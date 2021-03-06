#include "testing/testing.hpp"

#include "platform/http_request.hpp"
#include "platform/chunks_download_strategy.hpp"
#include "platform/platform.hpp"

#include "defines.hpp"

#include "coding/sha2.hpp"
#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"
#include "coding/internal/file_data.hpp"

#include "base/logging.hpp"
#include "base/std_serialization.hpp"

#include "std/bind.hpp"
#include "std/unique_ptr.hpp"

#include <QtCore/QCoreApplication>


#define TEST_URL1 "http://localhost:34568/unit_tests/1.txt"
#define TEST_URL_404 "http://localhost:34568/unit_tests/notexisting_unittest"
#define TEST_URL_PERMANENT "http://localhost:34568/unit_tests/permanent"
#define TEST_URL_INVALID_HOST "http://not-valid-host123532.ath.cx"
#define TEST_URL_BIG_FILE "http://localhost:34568/unit_tests/47kb.file"
#define TEST_URL_HTTPS "https://github.com"
#define TEST_URL_POST "http://localhost:34568/unit_tests/post.php"

using namespace downloader;


class DownloadObserver
{
  bool m_progressWasCalled;
  unique_ptr<HttpRequest::StatusT> m_status;
  // Interrupt download after this number of chunks
  int m_chunksToFail;

public:
  DownloadObserver() : m_chunksToFail(-1)
  {
    Reset();
    my::g_LogLevel = LDEBUG;
  }

  void CancelDownloadOnGivenChunk(int chunksToFail)
  {
    m_chunksToFail = chunksToFail;
  }

  void Reset()
  {
    m_progressWasCalled = false;
    m_status.reset();
  }

  void TestOk()
  {
    TEST(m_progressWasCalled, ("Download progress wasn't called"));
    TEST(m_status.get(), ());
    TEST_EQUAL(*m_status, HttpRequest::ECompleted, ());
  }

  void TestFailed()
  {
    TEST(m_status.get(), ());
    TEST_EQUAL(*m_status, HttpRequest::EFailed, ());
  }

  void OnDownloadProgress(HttpRequest & request)
  {
    m_progressWasCalled = true;
    TEST_EQUAL(request.Status(), HttpRequest::EInProgress, ());

    // Cancel download if needed
    if (m_chunksToFail != -1)
    {
      --m_chunksToFail;
      if (m_chunksToFail == 0)
      {
        m_chunksToFail = -1;
        LOG(LINFO, ("Download canceled"));
        QCoreApplication::quit();
      }
    }
  }

  void OnDownloadFinish(HttpRequest & request)
  {
    TEST(!m_status.get(), ());
    m_status.reset(new HttpRequest::StatusT(request.Status()));
    TEST(*m_status == HttpRequest::EFailed || *m_status == HttpRequest::ECompleted, ());
    QCoreApplication::quit();
  }

};

struct CancelDownload
{
  void OnProgress(HttpRequest & request)
  {
    TEST_GREATER(request.Data().size(), 0, ());
    delete &request;
    QCoreApplication::quit();
  }
  void OnFinish(HttpRequest &)
  {
    TEST(false, ("Should be never called"));
  }
};

struct DeleteOnFinish
{
  void OnProgress(HttpRequest & request)
  {
    TEST_GREATER(request.Data().size(), 0, ());
  }
  void OnFinish(HttpRequest & request)
  {
    delete &request;
    QCoreApplication::quit();
  }
};

UNIT_TEST(DownloaderSimpleGet)
{
  DownloadObserver observer;
  HttpRequest::CallbackT onFinish = bind(&DownloadObserver::OnDownloadFinish, &observer, _1);
  HttpRequest::CallbackT onProgress = bind(&DownloadObserver::OnDownloadProgress, &observer, _1);
  {
    // simple success case
    unique_ptr<HttpRequest> const request(HttpRequest::Get(TEST_URL1, onFinish, onProgress));
    // wait until download is finished
    QCoreApplication::exec();
    observer.TestOk();
    TEST_EQUAL(request->Data(), "Test1", ());
  }

  observer.Reset();
  {
    // We DO NOT SUPPORT redirects to avoid data corruption when downloading mwm files
    unique_ptr<HttpRequest> const request(HttpRequest::Get(TEST_URL_PERMANENT, onFinish, onProgress));
    QCoreApplication::exec();
    observer.TestFailed();
    TEST_EQUAL(request->Data().size(), 0, (request->Data()));
  }

  observer.Reset();
  {
    // fail case 404
    unique_ptr<HttpRequest> const request(HttpRequest::Get(TEST_URL_404, onFinish, onProgress));
    QCoreApplication::exec();
    observer.TestFailed();
    TEST_EQUAL(request->Data().size(), 0, (request->Data()));
  }

  observer.Reset();
  {
    // fail case not existing host
    unique_ptr<HttpRequest> const request(HttpRequest::Get(TEST_URL_INVALID_HOST, onFinish, onProgress));
    QCoreApplication::exec();
    observer.TestFailed();
    TEST_EQUAL(request->Data().size(), 0, (request->Data()));
  }

  {
    // cancel download in the middle of the progress
    CancelDownload canceler;
    /// should be deleted in canceler
    HttpRequest::Get(TEST_URL_BIG_FILE,
        bind(&CancelDownload::OnFinish, &canceler, _1),
        bind(&CancelDownload::OnProgress, &canceler, _1));
    QCoreApplication::exec();
  }

  observer.Reset();
  {
    // https success case
    unique_ptr<HttpRequest> const request(HttpRequest::Get(TEST_URL_HTTPS, onFinish, onProgress));
    // wait until download is finished
    QCoreApplication::exec();
    observer.TestOk();
    TEST_GREATER(request->Data().size(), 0, ());
  }

  {
    // Delete request at the end of successful download
    DeleteOnFinish deleter;
    // should be deleted in deleter on finish
    HttpRequest::Get(TEST_URL1,
        bind(&DeleteOnFinish::OnFinish, &deleter, _1),
        bind(&DeleteOnFinish::OnProgress, &deleter, _1));
    QCoreApplication::exec();
  }
}

UNIT_TEST(DownloaderSimplePost)
{
  DownloadObserver observer;
  HttpRequest::CallbackT onFinish = bind(&DownloadObserver::OnDownloadFinish, &observer, _1);
  HttpRequest::CallbackT onProgress = bind(&DownloadObserver::OnDownloadProgress, &observer, _1);
  {
    // simple success case
    string const postData = "{\"jsonKey\":\"jsonValue\"}";
    unique_ptr<HttpRequest> const request(HttpRequest::PostJson(TEST_URL_POST, postData, onFinish, onProgress));
    // wait until download is finished
    QCoreApplication::exec();
    observer.TestOk();
    TEST_EQUAL(request->Data(), postData, ());
  }
}

UNIT_TEST(ChunksDownloadStrategy)
{
  string const S1 = "UrlOfServer1";
  string const S2 = "UrlOfServer2";
  string const S3 = "UrlOfServer3";

  typedef pair<int64_t, int64_t> RangeT;
  RangeT const R1(0, 249), R2(250, 499), R3(500, 749), R4(750, 800);

  vector<string> servers;
  servers.push_back(S1);
  servers.push_back(S2);
  servers.push_back(S3);

  int64_t const FILE_SIZE = 800;
  int64_t const CHUNK_SIZE = 250;
  ChunksDownloadStrategy strategy(servers);
  strategy.InitChunks(FILE_SIZE, CHUNK_SIZE);

  string s1;
  RangeT r1;
  TEST_EQUAL(strategy.NextChunk(s1, r1), ChunksDownloadStrategy::ENextChunk, ());

  string s2;
  RangeT r2;
  TEST_EQUAL(strategy.NextChunk(s2, r2), ChunksDownloadStrategy::ENextChunk, ());

  string s3;
  RangeT r3;
  TEST_EQUAL(strategy.NextChunk(s3, r3), ChunksDownloadStrategy::ENextChunk, ());

  string sEmpty;
  RangeT rEmpty;
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());

  TEST(s1 != s2 && s2 != s3 && s3 != s1, (s1, s2, s3));

  TEST(r1 != r2 && r2 != r3 && r3 != r1, (r1, r2, r3));
  TEST(r1 == R1 || r1 == R2 || r1 == R3 || r1 == R4, (r1));
  TEST(r2 == R1 || r2 == R2 || r2 == R3 || r2 == R4, (r2));
  TEST(r3 == R1 || r3 == R2 || r3 == R3 || r3 == R4, (r3));

  strategy.ChunkFinished(true, r1);

  string s4;
  RangeT r4;
  TEST_EQUAL(strategy.NextChunk(s4, r4), ChunksDownloadStrategy::ENextChunk, ());
  TEST_EQUAL(s4, s1, ());
  TEST(r4 != r1 && r4 != r2 && r4 != r3, (r4));

  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(false, r2);

  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(true, r4);

  string s5;
  RangeT r5;
  TEST_EQUAL(strategy.NextChunk(s5, r5), ChunksDownloadStrategy::ENextChunk, ());
  TEST_EQUAL(s5, s4, (s5, s4));
  TEST_EQUAL(r5, r2, ());

  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(true, r5);

  // 3rd is still alive here
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(true, r3);

  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::EDownloadSucceeded, ());
  TEST_EQUAL(strategy.NextChunk(sEmpty, rEmpty), ChunksDownloadStrategy::EDownloadSucceeded, ());
}

UNIT_TEST(ChunksDownloadStrategyFAIL)
{
  string const S1 = "UrlOfServer1";
  string const S2 = "UrlOfServer2";

  typedef pair<int64_t, int64_t> RangeT;
  RangeT const R1(0, 249), R2(250, 499), R3(500, 749), R4(750, 800);

  vector<string> servers;
  servers.push_back(S1);
  servers.push_back(S2);

  int64_t const FILE_SIZE = 800;
  int64_t const CHUNK_SIZE = 250;
  ChunksDownloadStrategy strategy(servers);
  strategy.InitChunks(FILE_SIZE, CHUNK_SIZE);

  string s1;
  RangeT r1;
  TEST_EQUAL(strategy.NextChunk(s1, r1), ChunksDownloadStrategy::ENextChunk, ());

  string s2;
  RangeT r2;
  TEST_EQUAL(strategy.NextChunk(s2, r2), ChunksDownloadStrategy::ENextChunk, ());
  TEST_EQUAL(strategy.NextChunk(s2, r2), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(false, r1);

  TEST_EQUAL(strategy.NextChunk(s2, r2), ChunksDownloadStrategy::ENoFreeServers, ());

  strategy.ChunkFinished(false, r2);

  TEST_EQUAL(strategy.NextChunk(s2, r2), ChunksDownloadStrategy::EDownloadFailed, ());
}

namespace
{
  string ReadFileAsString(string const & file)
  {
    try
    {
      FileReader f(file);
      string s;
      f.ReadAsString(s);
      return s;
    }
    catch (FileReader::Exception const &)
    {
      TEST(false, ("File ", file, " should exist"));
      return string();
    }
  }

  void FinishDownloadSuccess(string const & file)
  {
    TEST(my::DeleteFileX(file), ("Result file should present on success"));

    uint64_t size;
    TEST(!my::GetFileSize(file + DOWNLOADING_FILE_EXTENSION, size), ("No downloading file on success"));
    TEST(!my::GetFileSize(file + RESUME_FILE_EXTENSION, size), ("No resume file on success"));
  }

  void FinishDownloadFail(string const & file)
  {
    uint64_t size;
    TEST(!my::GetFileSize(file, size), ("No result file on fail"));

    (void)my::DeleteFileX(file + DOWNLOADING_FILE_EXTENSION);

    TEST(my::DeleteFileX(file + RESUME_FILE_EXTENSION), ("Resume file should present on fail"));
  }
}

namespace
{
  void DeleteTempDownloadFiles()
  {
    // Remove data from previously failed files.

    // Get regexp like this: (\.downloading3$|\.resume3$)
    string const regexp = "(\\" RESUME_FILE_EXTENSION "$|\\" DOWNLOADING_FILE_EXTENSION "$)";

    Platform::FilesList files;
    Platform::GetFilesByRegExp(".", regexp, files);
    for (Platform::FilesList::iterator it = files.begin(); it != files.end(); ++it)
      FileWriter::DeleteFileX(*it);
  }
}

UNIT_TEST(DownloadChunks)
{
  string const FILENAME = "some_downloader_test_file";

  // remove data from previously failed files
  DeleteTempDownloadFiles();

  DownloadObserver observer;
  HttpRequest::CallbackT onFinish = bind(&DownloadObserver::OnDownloadFinish, &observer, _1);
  HttpRequest::CallbackT onProgress = bind(&DownloadObserver::OnDownloadProgress, &observer, _1);

  vector<string> urls;
  urls.push_back(TEST_URL1);
  urls.push_back(TEST_URL1);
  int64_t FILESIZE = 5;

  {
    // should use only one thread
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         onFinish, onProgress));
    // wait until download is finished
    QCoreApplication::exec();

    observer.TestOk();

    TEST_EQUAL(request->Data(), FILENAME, ());
    TEST_EQUAL(ReadFileAsString(FILENAME), "Test1", ());

    FinishDownloadSuccess(FILENAME);
  }

  observer.Reset();

  urls.clear();
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL_BIG_FILE);
  FILESIZE = 5;

  {
    // 3 threads - fail, because of invalid size
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         onFinish, onProgress, 2048));
    // wait until download is finished
    QCoreApplication::exec();

    observer.TestFailed();

    FinishDownloadFail(FILENAME);
  }

  string const SHA256 = "49F7BC24B6137C339DFE2D538EE533C7DC6AF89FACBCCE750D7B682C77D61FB1";

  observer.Reset();

  urls.clear();
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL_BIG_FILE);
  FILESIZE = 47684;

  {
    // 3 threads - succeeded
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         onFinish, onProgress, 2048));
    // wait until download is finished
    QCoreApplication::exec();

    observer.TestOk();

    TEST_EQUAL(sha2::digest256(ReadFileAsString(FILENAME)), SHA256, ());

    FinishDownloadSuccess(FILENAME);
  }

  observer.Reset();

  urls.clear();
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL1);
  urls.push_back(TEST_URL_404);
  FILESIZE = 47684;

  {
    // 3 threads with only one valid url - succeeded
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         onFinish, onProgress, 2048));

    // wait until download is finished
    QCoreApplication::exec();

    observer.TestOk();

    TEST_EQUAL(sha2::digest256(ReadFileAsString(FILENAME)), SHA256, ());

    FinishDownloadSuccess(FILENAME);
  }

  observer.Reset();

  urls.clear();
  urls.push_back(TEST_URL_BIG_FILE);
  urls.push_back(TEST_URL_BIG_FILE);
  FILESIZE = 12345;

  {
    // 2 threads and all points to file with invalid size - fail
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         onFinish, onProgress, 2048));
    // wait until download is finished
    QCoreApplication::exec();

    observer.TestFailed();

    FinishDownloadFail(FILENAME);
  }
}


namespace
{
  int64_t FILESIZE = 47684;
  int64_t const beg1 = 123, end1 = 1230, beg2 = 44000, end2 = 47683;

  struct ResumeChecker
  {
    size_t m_counter;
    ResumeChecker() : m_counter(0) {}

    void OnProgress(HttpRequest & request)
    {
      if (m_counter == 0)
      {
        TEST_EQUAL(request.Progress(), HttpRequest::ProgressT(beg2, FILESIZE), ());
      }
      else if (m_counter == 1)
      {
        TEST_EQUAL(request.Progress(), HttpRequest::ProgressT(FILESIZE, FILESIZE), ());
      }
      else
      {
        TEST(false, ("Progress should be called exactly 2 times"));
      }
      ++m_counter;
    }

    void OnFinish(HttpRequest & request)
    {
      TEST_EQUAL(request.Status(), HttpRequest::ECompleted, ());
      QCoreApplication::exit();
    }
  };
}

UNIT_TEST(DownloadResumeChunks)
{
  string const FILENAME = "some_test_filename_12345";
  string const RESUME_FILENAME = FILENAME + RESUME_FILE_EXTENSION;
  string const DOWNLOADING_FILENAME = FILENAME + DOWNLOADING_FILE_EXTENSION;
  string const SHA256 = "49F7BC24B6137C339DFE2D538EE533C7DC6AF89FACBCCE750D7B682C77D61FB1";

  // remove data from previously failed files
  DeleteTempDownloadFiles();

  vector<string> urls;
  urls.push_back(TEST_URL_BIG_FILE);

  // 1st step - download full file
  {
    DownloadObserver observer;

    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                            bind(&DownloadObserver::OnDownloadFinish, &observer, _1),
                            bind(&DownloadObserver::OnDownloadProgress, &observer, _1)));

    QCoreApplication::exec();

    observer.TestOk();

    TEST_EQUAL(sha2::digest256(ReadFileAsString(FILENAME)), SHA256, ());
    uint64_t size;
    TEST(!my::GetFileSize(RESUME_FILENAME, size), ("No resume file on success"));
  }

  // 2nd step - mark some file blocks as not downloaded
  {
    // to substitute temporary not fully downloaded file
    TEST(my::RenameFileX(FILENAME, DOWNLOADING_FILENAME), ());

    FileWriter f(DOWNLOADING_FILENAME, FileWriter::OP_WRITE_EXISTING);
    f.Seek(beg1);
    char b1[end1 - beg1 + 1] = {0};
    f.Write(b1, ARRAY_SIZE(b1));

    f.Seek(beg2);
    char b2[end2 - beg2 + 1] = {0};
    f.Write(b2, ARRAY_SIZE(b2));

    ChunksDownloadStrategy strategy((vector<string>()));
    strategy.AddChunk(make_pair(int64_t(0), beg1-1), ChunksDownloadStrategy::CHUNK_COMPLETE);
    strategy.AddChunk(make_pair(beg1, end1), ChunksDownloadStrategy::CHUNK_FREE);
    strategy.AddChunk(make_pair(end1+1, beg2-1), ChunksDownloadStrategy::CHUNK_COMPLETE);
    strategy.AddChunk(make_pair(beg2, end2), ChunksDownloadStrategy::CHUNK_FREE);

    strategy.SaveChunks(FILESIZE, RESUME_FILENAME);
  }

  // 3rd step - check that resume works
  {
    ResumeChecker checker;
    unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                                                         bind(&ResumeChecker::OnFinish, &checker, _1),
                                                         bind(&ResumeChecker::OnProgress, &checker, _1)));
    QCoreApplication::exec();

    TEST_EQUAL(sha2::digest256(ReadFileAsString(FILENAME)), SHA256, ());

    FinishDownloadSuccess(FILENAME);
  }
}

// Unit test with forcible canceling of http request
UNIT_TEST(DownloadResumeChunksWithCancel)
{
  string const FILENAME = "some_test_filename_12345";
  string const SHA256 = "49F7BC24B6137C339DFE2D538EE533C7DC6AF89FACBCCE750D7B682C77D61FB1";

  // remove data from previously failed files
  DeleteTempDownloadFiles();

  vector<string> urls;
  urls.push_back(TEST_URL_BIG_FILE);

  DownloadObserver observer;

  int arrCancelChunks[] = { 1, 3, 10, 15, 20, 0 };

  for (size_t i = 0; i < ARRAY_SIZE(arrCancelChunks); ++i)
  {
    if (arrCancelChunks[i] > 0)
      observer.CancelDownloadOnGivenChunk(arrCancelChunks[i]);

      unique_ptr<HttpRequest> const request(HttpRequest::GetFile(urls, FILENAME, FILESIZE,
                              bind(&DownloadObserver::OnDownloadFinish, &observer, _1),
                              bind(&DownloadObserver::OnDownloadProgress, &observer, _1),
                              1024, false));

      QCoreApplication::exec();
  }

  observer.TestOk();

  TEST_EQUAL(sha2::digest256(ReadFileAsString(FILENAME)), SHA256, ());

  FinishDownloadSuccess(FILENAME);
}
