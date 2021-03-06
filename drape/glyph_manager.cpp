#include "drape/glyph_manager.hpp"
#include "3party/sdf_image/sdf_image.h"

#include "platform/platform.hpp"

#include "coding/reader.hpp"

#include "base/string_utils.hpp"
#include "base/logging.hpp"
#include "base/math.hpp"
#include "base/timer.hpp"

#include <ft2build.h>
#include FT_TYPES_H
#include FT_SYSTEM_H
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_CACHE_H

#ifdef DEBUG
  #undef __FTERRORS_H__
  #define FT_ERRORDEF(e, v, s) {e, s},
  #define FT_ERROR_START_LIST  {
  #define FT_ERROR_END_LIST    {0, 0}};
  struct FreetypeError
  {
    int m_code;
    char const * m_message;
  } g_FT_Errors[]
  #include FT_ERRORS_H

  #define FREETYPE_CHECK(x) \
    do \
    { \
      FT_Error const err = (x); \
      if (err) \
        LOG(LWARNING, ("Freetype:", g_FT_Errors[err].m_code, g_FT_Errors[err].m_message)); \
    } while (false)

  #define FREETYPE_CHECK_RETURN(x, msg) \
    do \
    { \
      FT_Error const err = (x); \
      if (err) \
      { \
        LOG(LWARNING, ("Freetype", g_FT_Errors[err].m_code, g_FT_Errors[err].m_message, msg)); \
        return; \
      } \
    } while (false)
#else
  #define FREETYPE_CHECK(x) x
  #define FREETYPE_CHECK_RETURN(x, msg) FREETYPE_CHECK(x)
#endif

namespace dp
{

namespace
{

int const SDF_SCALE_FACTOR = 4;
int const SDF_BORDER = 4 * SDF_SCALE_FACTOR;

template <typename ToDo>
void ParseUniBlocks(string const & uniBlocksFile, ToDo toDo)
{
  string uniBlocks;
  try
  {
    ReaderPtr<Reader>(GetPlatform().GetReader(uniBlocksFile)).ReadAsString(uniBlocks);
  }
  catch (RootException const & e)
  {
    LOG(LCRITICAL, ("Error reading uniblock description: ", e.what()));
    return;
  }

  istringstream fin(uniBlocks);
  while (true)
  {
    string name;
    strings::UniChar start;
    strings::UniChar end;
    fin >> name >> std::hex >> start >> std::hex >> end;
    if (!fin)
      break;

    toDo(name, start, end);
  }
}

template <typename ToDo>
void ParseFontList(string const & fontListFile, ToDo toDo)
{
  string fontList;
  try
  {
    ReaderPtr<Reader>(GetPlatform().GetReader(fontListFile)).ReadAsString(fontList);
  }
  catch(RootException const & e)
  {
    LOG(LWARNING, ("Error reading font list ", fontListFile, " : ", e.what()));
    return;
  }

  istringstream fin(fontList);
  while (true)
  {
    string ubName;
    string fontName;
    fin >> ubName >> fontName;
    if (!fin)
      break;

    toDo(ubName, fontName);
  }
}

class Font
{
public:
  Font(ReaderPtr<Reader> fontReader, FT_Library lib)
    : m_fontReader(fontReader)
  {
    m_stream.base = 0;
    m_stream.size = m_fontReader.Size();
    m_stream.pos = 0;
    m_stream.descriptor.pointer = &m_fontReader;
    m_stream.pathname.pointer = 0;
    m_stream.read = &Font::Read;
    m_stream.close = &Font::Close;
    m_stream.memory = 0;
    m_stream.cursor = 0;
    m_stream.limit = 0;

    FT_Open_Args args;
    args.flags = FT_OPEN_STREAM;
    args.memory_base = 0;
    args.memory_size = 0;
    args.pathname = 0;
    args.stream = &m_stream;
    args.driver = 0;
    args.num_params = 0;
    args.params = 0;

    FREETYPE_CHECK(FT_Open_Face(lib, &args, 0, &m_fontFace));
  }

  void DestroyFont()
  {
    FREETYPE_CHECK(FT_Done_Face(m_fontFace));
    m_fontFace = nullptr;
  }

  bool HasGlyph(strings::UniChar unicodePoint) const
  {
    return FT_Get_Char_Index(m_fontFace, unicodePoint) != 0;
  }

  GlyphManager::Glyph GetGlyph(strings::UniChar unicodePoint, uint32_t baseHeight) const
  {
    FREETYPE_CHECK(FT_Set_Pixel_Sizes(m_fontFace, SDF_SCALE_FACTOR * baseHeight, SDF_SCALE_FACTOR * baseHeight));
    FREETYPE_CHECK(FT_Load_Glyph(m_fontFace, FT_Get_Char_Index(m_fontFace, unicodePoint), FT_LOAD_RENDER));

    FT_Glyph glyph;
    FREETYPE_CHECK(FT_Get_Glyph(m_fontFace->glyph, &glyph));

    FT_BBox bbox;
    FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS , &bbox);

    FT_Bitmap bitmap = m_fontFace->glyph->bitmap;

    SharedBufferManager::shared_buffer_ptr_t data;
    size_t bufferSize = 0;
    int imgWidth = bitmap.width;
    int imgHeigh = bitmap.rows;
    if (bitmap.buffer != nullptr)
    {
      SdfImage img(bitmap.rows, bitmap.pitch, bitmap.buffer, SDF_BORDER);
      img.GenerateSDF(1.0f / (float)SDF_SCALE_FACTOR);

      imgWidth = img.GetWidth();
      imgHeigh = img.GetHeight();

      size_t byteSize = imgWidth * imgHeigh;
      bufferSize = my::NextPowOf2(byteSize);
      data = SharedBufferManager::instance().reserveSharedBuffer(bufferSize);

      img.GetData(*data);
    }

    GlyphManager::Glyph result;
    result.m_image = GlyphManager::GlyphImage
    {
      imgWidth,
      imgHeigh,
      data
    };

    result.m_metrics = GlyphManager::GlyphMetrics
    {
      static_cast<float>(glyph->advance.x >> 16) / SDF_SCALE_FACTOR,
      static_cast<float>(glyph->advance.y >> 16) / SDF_SCALE_FACTOR,
      static_cast<float>(bbox.xMin) / SDF_SCALE_FACTOR,
      static_cast<float>(bbox.yMin) / SDF_SCALE_FACTOR,
      true
    };

    FT_Done_Glyph(glyph);

    return result;
  }

  void GetCharcodes(vector<FT_ULong> & charcodes)
  {
    FT_UInt gindex;
    charcodes.push_back(FT_Get_First_Char(m_fontFace, &gindex));
    while (gindex)
      charcodes.push_back(FT_Get_Next_Char(m_fontFace, charcodes.back(), &gindex));

    sort(charcodes.begin(), charcodes.end());
    charcodes.erase(unique(charcodes.begin(), charcodes.end()), charcodes.end());
  }

  static unsigned long Read(FT_Stream stream, unsigned long offset, unsigned char * buffer, unsigned long count)
  {
    if (count != 0)
    {
      ReaderPtr<Reader> * reader = reinterpret_cast<ReaderPtr<Reader> *>(stream->descriptor.pointer);
      reader->Read(offset, buffer, count);
    }

    return count;
  }

  static void Close(FT_Stream){}

private:
  ReaderPtr<Reader> m_fontReader;
  FT_StreamRec_ m_stream;
  FT_Face m_fontFace;
};

}

/// Information about single unicode block.
struct UnicodeBlock
{
  string m_name;

  strings::UniChar m_start;
  strings::UniChar m_end;
  vector<int> m_fontsWeight;

  UnicodeBlock(string const & name, strings::UniChar start, strings::UniChar end)
    : m_name(name)
    , m_start(start)
    , m_end(end)
  {
  }

  int GetFontOffset(int idx) const
  {
    ASSERT(!m_fontsWeight.empty(), ());
    int maxWight = 0;
    int upperBoundWeight = numeric_limits<int>::max();
    if (idx != -1)
      upperBoundWeight = m_fontsWeight[idx];

    int index = -1;
    for (size_t i = 0; i < m_fontsWeight.size(); ++i)
    {
      int w = m_fontsWeight[i];
      if (w < upperBoundWeight && w > maxWight)
      {
        maxWight = w;
        index = i;
      }
    }

    return index;
  }

  bool HasSymbol(strings::UniChar sym) const
  {
    return (m_start <= sym) && (m_end >= sym);
  }
};

typedef vector<UnicodeBlock> TUniBlocks;
typedef TUniBlocks::const_iterator TUniBlockIter;

struct GlyphManager::Impl
{
  FT_Library m_library;
  TUniBlocks m_blocks;
  TUniBlockIter m_lastUsedBlock;
  vector<Font> m_fonts;

  uint32_t m_baseGlyphHeight;
};

GlyphManager::GlyphManager(GlyphManager::Params const & params)
  : m_impl(new Impl())
{
  m_impl->m_baseGlyphHeight = params.m_baseGlyphHeight;

  typedef pair<string, string> TFontAndBlockName;
  typedef buffer_vector<TFontAndBlockName, 64> TFontLst;

  TFontLst whitelst;
  TFontLst blacklst;

  m_impl->m_blocks.reserve(160);
  ParseUniBlocks(params.m_uniBlocks, [this](string const & name, strings::UniChar start, strings::UniChar end)
  {
    m_impl->m_blocks.push_back(UnicodeBlock(name, start, end));
  });

  ParseFontList(params.m_whitelist, [&whitelst](string const & ubName, string const & fontName)
  {
    whitelst.push_back(TFontAndBlockName(fontName, ubName));
  });

  ParseFontList(params.m_blacklist, [&blacklst](string const & ubName, string const & fontName)
  {
    blacklst.push_back(TFontAndBlockName(fontName, ubName));
  });

  m_impl->m_fonts.reserve(params.m_fonts.size());

  FREETYPE_CHECK(FT_Init_FreeType(&m_impl->m_library));

  for (string const & fontName : params.m_fonts)
  {
    bool ignoreFont = false;
    for_each(blacklst.begin(), blacklst.end(), [&ignoreFont, &fontName](TFontAndBlockName const & p)
    {
      if (p.first == fontName && p.second == "*")
        ignoreFont = true;
    });

    if (ignoreFont)
      continue;

    vector<FT_ULong> charCodes;
    try
    {
      m_impl->m_fonts.emplace_back(GetPlatform().GetReader(fontName), m_impl->m_library);
      m_impl->m_fonts.back().GetCharcodes(charCodes);
    }
    catch(RootException const & e)
    {
      LOG(LWARNING, ("Error read font file : ", e.what()));
    }

    typedef size_t TBlockIndex;
    typedef int TCharCounter;
    typedef pair<TBlockIndex, TCharCounter> TCoverNode;
    typedef vector<TCoverNode> TCoverInfo;

    size_t currentUniBlock = 0;
    TCoverInfo coverInfo;
    for (FT_ULong const & charCode : charCodes)
    {
      while(currentUniBlock < m_impl->m_blocks.size())
      {
        if (m_impl->m_blocks[currentUniBlock].HasSymbol(charCode))
          break;
        ++currentUniBlock;
      }

      if (currentUniBlock >= m_impl->m_blocks.size())
        break;

      if (coverInfo.empty() || coverInfo.back().first != currentUniBlock)
        coverInfo.push_back(make_pair(currentUniBlock, 1));
      else
        ++coverInfo.back().second;
    }

    typedef function<void (UnicodeBlock const & uniBlock, TCoverNode & node)> TUpdateCoverInfoFn;
    auto enumerateFn = [this, &coverInfo, &fontName] (TFontLst const & lst, TUpdateCoverInfoFn const & fn)
    {
      for (TFontAndBlockName const & b : lst)
      {
        if (b.first != fontName)
          continue;

        for (TCoverNode & node : coverInfo)
        {
          UnicodeBlock const & uniBlock = m_impl->m_blocks[node.first];
          if (uniBlock.m_name == b.second)
          {
            fn(uniBlock, node);
            break;
          }
          else if (b.second == "*")
            fn(uniBlock, node);
        }
      }
    };

    enumerateFn(blacklst, [](UnicodeBlock const &, TCoverNode & node)
    {
      node.second = 0;
    });

    enumerateFn(whitelst, [this](UnicodeBlock const & uniBlock, TCoverNode & node)
    {
      node.second = uniBlock.m_end + 1 - uniBlock.m_start + m_impl->m_fonts.size();
    });

    for (TCoverNode & node : coverInfo)
    {
      UnicodeBlock & uniBlock = m_impl->m_blocks[node.first];
      uniBlock.m_fontsWeight.resize(m_impl->m_fonts.size(), 0);
      uniBlock.m_fontsWeight.back() = node.second;
    }
  }

  m_impl->m_lastUsedBlock = m_impl->m_blocks.end();
}

GlyphManager::~GlyphManager()
{
  for (Font & f : m_impl->m_fonts)
    f.DestroyFont();

  FREETYPE_CHECK(FT_Done_FreeType(m_impl->m_library));
  delete m_impl;
}

GlyphManager::Glyph GlyphManager::GetGlyph(strings::UniChar unicodePoint)
{
  TUniBlockIter iter = m_impl->m_blocks.end();
  if (m_impl->m_lastUsedBlock != m_impl->m_blocks.end() && m_impl->m_lastUsedBlock->HasSymbol(unicodePoint))
    iter = m_impl->m_lastUsedBlock;
  else
  {
    if (iter == m_impl->m_blocks.end() || !iter->HasSymbol(unicodePoint))
    {
      iter = lower_bound(m_impl->m_blocks.begin(), m_impl->m_blocks.end(), unicodePoint,
                         [](UnicodeBlock const & block, strings::UniChar const & v)
      {
        return block.m_end < v;
      });
    }
  }

  if (iter == m_impl->m_blocks.end() || !iter->HasSymbol(unicodePoint))
    return GetInvalidGlyph();

  m_impl->m_lastUsedBlock = iter;

  int fontIndex = -1;
  UnicodeBlock const & block = *iter;
  ASSERT(block.HasSymbol(unicodePoint), ());
  do
  {
    if (fontIndex != -1)
    {
      ASSERT_LESS(fontIndex, m_impl->m_fonts.size(), ());
      Font const & f = m_impl->m_fonts[fontIndex];
      if (f.HasGlyph(unicodePoint))
        return f.GetGlyph(unicodePoint, m_impl->m_baseGlyphHeight);
    }

    fontIndex = block.GetFontOffset(fontIndex);
  } while(fontIndex != -1);

  return GetInvalidGlyph();
}

void GlyphManager::ForEachUnicodeBlock(GlyphManager::TUniBlockCallback const & fn) const
{
  for (UnicodeBlock const & uni : m_impl->m_blocks)
    fn(uni.m_start, uni.m_end);
}

GlyphManager::Glyph GlyphManager::GetInvalidGlyph() const
{
  static bool s_inited = false;
  static Glyph s_glyph;

  if (!s_inited)
  {
    ASSERT(!m_impl->m_fonts.empty(), ());
    s_glyph = m_impl->m_fonts[0].GetGlyph(0x9, m_impl->m_baseGlyphHeight);
    s_glyph.m_metrics.m_isValid = false;
    s_inited = true;
  }

  return s_glyph;
}

}
