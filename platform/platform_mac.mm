#include "platform.hpp"

#include "../base/logging.hpp"

#include "../std/target_os.hpp"

#include <IOKit/IOKitLib.h>
#include <Foundation/NSBundle.h>
#include <Foundation/NSPathUtilities.h>
#include <Foundation/NSAutoreleasePool.h>

#include <sys/stat.h>
#include <sys/sysctl.h>

#include <dispatch/dispatch.h>


Platform::Platform()
{
  NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];

  // get resources directory path
  string const resourcesPath = [[[NSBundle mainBundle] resourcePath] UTF8String];
  string const bundlePath = [[[NSBundle mainBundle] bundlePath] UTF8String];
  if (resourcesPath == bundlePath)
  {
    // we're the console app, probably unit test, and path is our directory
    m_resourcesDir = bundlePath + "/../../data/";
    m_writableDir = m_resourcesDir;
  }
  else
  {
    m_resourcesDir = resourcesPath + "/";

    // get writable path
#ifndef OMIM_PRODUCTION
    // developers can have symlink to data folder
    char const * dataPath = "../../../../../data/";
    if (IsFileExistsByFullPath(m_resourcesDir + dataPath))
      m_writableDir = m_resourcesDir + dataPath;
#endif

    if (m_writableDir.empty())
    {
      NSArray * dirPaths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
      NSString * supportDir = [dirPaths objectAtIndex:0];
      m_writableDir = [supportDir UTF8String];
      m_writableDir += "/MapsWithMe/";
      ::mkdir(m_writableDir.c_str(), 0755);
    }
  }
  [pool release];

  m_settingsDir = m_writableDir;

  NSString * tempDir = NSTemporaryDirectory();
  if (tempDir == nil)
      tempDir = @"/tmp";
  m_tmpDir = [tempDir UTF8String];
  m_tmpDir += '/';

  LOG(LDEBUG, ("Resources Directory:", m_resourcesDir));
  LOG(LDEBUG, ("Writable Directory:", m_writableDir));
  LOG(LDEBUG, ("Tmp Directory:", m_tmpDir));
  LOG(LDEBUG, ("Settings Directory:", m_settingsDir));
}

int Platform::CpuCores() const
{
  int mib[2], numCPU = 0;
  size_t len = sizeof(numCPU);
  mib[0] = CTL_HW;
  mib[1] = HW_AVAILCPU;
  sysctl(mib, 2, &numCPU, &len, NULL, 0);
  if (numCPU >= 1)
    return numCPU;
  // second try
  mib[1] = HW_NCPU;
  len = sizeof(numCPU);
  sysctl(mib, 2, &numCPU, &len, NULL, 0);
  if (numCPU >= 1)
    return numCPU;
  return 1;
}

string Platform::UniqueClientId() const
{
  io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
  CFStringRef uuidCf = (CFStringRef) IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
  IOObjectRelease(ioRegistryRoot);
  char buf[513];
  CFStringGetCString(uuidCf, buf, 512, kCFStringEncodingUTF8);
  CFRelease(uuidCf);

  return HashUniqueID(buf);
}

static void PerformImpl(void * obj)
{
  Platform::TFunctor * f = reinterpret_cast<Platform::TFunctor *>(obj);
  (*f)();
  delete f;
}

void Platform::RunOnGuiThread(TFunctor const & fn)
{
  dispatch_async_f(dispatch_get_main_queue(), new TFunctor(fn), &PerformImpl);
}

void Platform::RunAsync(TFunctor const & fn, Priority p)
{
  int priority = DISPATCH_QUEUE_PRIORITY_DEFAULT;
  switch (p)
  {
    case EPriorityDefault: priority = DISPATCH_QUEUE_PRIORITY_DEFAULT; break;
    case EPriorityHigh: priority = DISPATCH_QUEUE_PRIORITY_HIGH; break;
    case EPriorityLow: priority = DISPATCH_QUEUE_PRIORITY_LOW; break;
    // It seems like this option is not supported in Snow Leopard.
    //case EPriorityBackground: priority = DISPATCH_QUEUE_PRIORITY_BACKGROUND; break;
    default: priority = INT16_MIN;
  }
  dispatch_async_f(dispatch_get_global_queue(priority, 0), new TFunctor(fn), &PerformImpl);
}
