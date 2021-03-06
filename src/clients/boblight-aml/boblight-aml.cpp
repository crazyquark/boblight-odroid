/*
* boblight
* Copyright (C) Bob  2009 
* 
* boblight is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* boblight is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along
* with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define BOBLIGHT_DLOPEN
#include "lib/boblight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <algorithm>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "config.h"
#include "util/misc.h"
#include "util/timeutils.h"
#include "flagmanager-aml.h"

using namespace std;

//from linux/amlogic/amports/amvideocap.h
#define AMVIDEOCAP_IOC_MAGIC 'V'
#define AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH      _IOW(AMVIDEOCAP_IOC_MAGIC, 0x02, int)
#define AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT     _IOW(AMVIDEOCAP_IOC_MAGIC, 0x03, int)


// helper class - tries to load the "movie" settings from the script.xbmc.boblight addon
// and pass them to the boblight-aml client
class CBoblightAddonSettings
{
 public:
   CBoblightAddonSettings() : m_bobdisable(false), m_settingsLoaded(false)
   { 
     m_settingsLoaded = loadBoblightAddonSettings();
   }

   std::string getBoblightClientCmdLine()
   {
     std::string cmdLine = "";
     //convert bool string to lowercase
     transform(m_interpolation.begin(), m_interpolation.end(), m_interpolation.begin(), ::tolower);

     cmdLine += "-s " + m_ip + ":" + m_port;
     cmdLine += " -o autospeed=" + m_autospeed;
     cmdLine += " -o interpolation=" + m_interpolation;
     cmdLine += " -o saturation=" + m_saturation;
     cmdLine += " -o speed=" + m_speed;
     cmdLine += " -o threshold=" + m_threshold;
     cmdLine += " -o value=" + m_value;
     return cmdLine;
   }

   bool m_bobdisable;
   bool m_settingsLoaded;
   std::string m_ip;
   std::string m_port;
   std::string m_autospeed;
   std::string m_interpolation;
   std::string m_saturation;
   std::string m_speed;
   std::string m_threshold;
   std::string m_value;

 private:
   #define SETTINGS_ATTR_BOBDISABLE     "bobdisable"
   #define SETTINGS_ATTR_IP             "hostip"
   #define SETTINGS_ATTR_PORT           "hostport"
   #define SETTINGS_ATTR_AUTOSPEED      "movie_autospeed"
   #define SETTINGS_ATTR_INTERPOLATION  "movie_interpolation"
   #define SETTINGS_ATTR_SATURATION     "movie_saturation"
   #define SETTINGS_ATTR_SPEED          "movie_speed"
   #define SETTINGS_ATTR_THRESHOLD      "movie_threshold"
   #define SETTINGS_ATTR_VALUE          "movie_value"
   #define KODI_HOME_ENV_VAR            "HOME"

   bool loadBoblightAddonSettings()
   {
     bool ret = false;
     //char *kodiHome = getenv(KODI_HOME_ENV_VAR);
     //fallback to custom settings file in case boblight addon is not installed
     std::string settingsFile = "/mnt/sdcard0/Android/data/org.xbmc.kodi/files/.kodi/addons/script.xbmc.boblight/resources/settings.xml";

     //if (kodiHome != NULL)
     //{
     //  settingsFile = std::string(kodiHome) + "/.kodi/userdata/addon_data/script.xbmc.boblight/settings.xml";
     //}

     FILE *fd = fopen(settingsFile.c_str(), "r");

     if (fd != NULL)
     {
       fseek(fd, 0, SEEK_END);
       size_t fileSize = ftell(fd);
       fseek(fd, 0, SEEK_SET);
       if (fileSize > 0)
       {
         if (fileSize > 32000)//read 16k max - there shouldn't be a bigger settings.xml from boblight [tm]
           fileSize = 32000;
         char *xmlBuffer = new char[fileSize];
         size_t readCount = fread(xmlBuffer, fileSize, 1, fd);
         fclose(fd);

         if (readCount == 1)
         {
           parseBoblightSettings(std::string(xmlBuffer));
           ret = true;
         }
         else
         {
           fprintf(stderr, "Failed reading boblight addon settings.xml");
         }
         delete[] xmlBuffer;
       }
     }
     return ret;
   }

   void parseBoblightSettings(std::string xmlBuffer)
   {
     std::string settings_bobdisable_str;
     settings_bobdisable_str = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_BOBDISABLE);
     if (settings_bobdisable_str == "true" || settings_bobdisable_str == "True")
       m_bobdisable = true;

     m_ip = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_IP);
     m_port = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_PORT);
     m_autospeed = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_AUTOSPEED);
     m_interpolation = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_INTERPOLATION);
     m_saturation = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_SATURATION);
     m_speed = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_SPEED);
     m_threshold = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_THRESHOLD);
     m_value = getValueFromXmlBuffer(xmlBuffer, SETTINGS_ATTR_VALUE);
   }

   std::string getValueFromXmlBuffer(const std::string &xmlBuffer, const char* xmlAttribute)
   {
     size_t strPos = 0;
     std::string valueStr;

     // each line in the xml looks like this:
     // <setting id="movie_value" value="1.000006" />
     // find the attribute
     if ((strPos = xmlBuffer.find(xmlAttribute)) != std::string::npos)
     {
       size_t strPos2 = 0;
       // from movie_value" value="1.000006" /> look for "value"
       if ((strPos2 = xmlBuffer.find("value", strPos)) != std::string::npos)
       {
         size_t strPos3 = 0;
         // from value="1.000006" /> look for "="
         if ((strPos3 = xmlBuffer.find("=", strPos2)) != std::string::npos)
         {
           //extract the value - strPos3 points to ="1.000006"
           int valueOffset = 1; //skip the "="
           if (xmlBuffer[strPos3 + valueOffset] == '"')
             valueOffset++;//skip " if needed
           int strLen = 0;
           do
           {
             // value stops with " or space
             if (xmlBuffer[strPos3 + valueOffset + strLen] == '"' ||
               xmlBuffer[strPos3 + valueOffset + strLen] == ' ')
               break;
             strLen++;
           } while (strLen < 20);// no insane xml garbage ...

           valueStr = xmlBuffer.substr(strPos3 + valueOffset, strLen);
         }
       }
     }
     return valueStr;
   }
};

struct aml_snapshot_t {
 unsigned int  dst_width;
 unsigned int  dst_height;
 unsigned int  dst_stride;
 unsigned int  dst_size;
 void         *dst_vaddr;
};

volatile bool g_stop = false;
CFlagManagerAML g_flagmanager;
/*********************************************************
*********************************************************/
static void SignalHandler(int signum)
{
 if (signum == SIGTERM)
 {
   fprintf(stderr, "caught SIGTERM\n");
   g_stop = true;
 }
 else if (signum == SIGINT)
 {
   fprintf(stderr, "caught SIGTERM\n");
   g_stop = true;
 }
}

#define VIDEO_PATH       "/dev/amvideo"
#define AMSTREAM_IOC_MAGIC  'S'
#define AMSTREAM_IOC_GET_VIDEO_DISABLE  _IOR(AMSTREAM_IOC_MAGIC, 0x48, unsigned long)
static int amvideo_utils_video_playing()
{
 int video_fd;
 int video_disable;

 video_fd = open(VIDEO_PATH, O_RDONLY);
 if (video_fd < 0) {
   return -1;
 }

 ioctl(video_fd, AMSTREAM_IOC_GET_VIDEO_DISABLE, &video_disable);
 if (video_disable)
 {
   close(video_fd);
   return 1;
 }

 close(video_fd);

//                     fprintf(stderr, "pos x %d y %d w %d h %d\n",snapshot.src_x, snapshot.src_y,snapshot.src_width,snapshot.src_height);
 return 0;
}

static int capture_frame(int fd, aml_snapshot_t &snapshot)
{
 int ret = 0;
 
 ssize_t readResult = pread(fd, snapshot.dst_vaddr, snapshot.dst_size, 0);

 if (readResult < snapshot.dst_size)
 {
   //fprintf(stderr, "frame read returned %d\n", readResult);
 }
 //fprintf(stderr, "requ: %d read %d \n", snapshot.dst_size, readResult);
 //fprintf(stderr, ".");
 return ret;
}

static int configure_capture(int fd, aml_snapshot_t &snapshot)
{
 int ret = 0;
 int ioctlret = 0;

 if ((ioctlret = ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH, snapshot.dst_width)) != 0)
 {
   ret = 2;
   fprintf(stderr, "Error setting frame width (ret: %d errno: %d)\n", ioctlret, errno);
 }
   
 
 if ((ioctlret = ioctl(fd, AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT, snapshot.dst_height)) != 0)
 {
   ret = 3;
   fprintf(stderr, "Error setting frame height (ret: %d errno: %d)\n", ioctlret, errno);
 }

 return ret;
}

static void frameToboblight(void *boblight, uint8_t* outputptr, int w, int h, int stride)
{
 if (!boblight)
 {
   fprintf(stderr, "no boblight\n");
   return;
 }
 if (!outputptr)
 {
   fprintf(stderr, "no outputptr\n");
   return;
 }
 //read out pixels and hand them to libboblight
 uint8_t* buffptr;
 for (int y = h; y > 0; y--) {
   buffptr = outputptr + stride * y;
   for (int x = 0; x < w; x++) {
     int rgb[3];
     rgb[2] = *(buffptr++);
     rgb[1] = *(buffptr++);
     rgb[0] = *(buffptr++);

     //fprintf(stdout, "frameToboblight: x(%d), y(%d)\n", x, y);

     boblight_addpixelxy(boblight, x, y, rgb);
   }
 }
}

static int Run(void* boblight)
{
 int snapshot_fd = -1;               
 aml_snapshot_t aml_snapshot = {0};
 int lastPriority = 255;

 aml_snapshot.dst_width  = 160;
 aml_snapshot.dst_height = 160;

 // calc stride, size and alloc mem
 aml_snapshot.dst_stride = aml_snapshot.dst_width  * 3;
 aml_snapshot.dst_size   = aml_snapshot.dst_stride * aml_snapshot.dst_height;
 aml_snapshot.dst_vaddr  = calloc(aml_snapshot.dst_size, 1);

 fprintf(stdout, "Connection to boblightd config: width(%d), height(%d)\n",
   aml_snapshot.dst_width, aml_snapshot.dst_height);
 //tell libboblight how big our image is
 boblight_setscanrange(boblight, (int)aml_snapshot.dst_width, (int)aml_snapshot.dst_height);

 while(!g_stop)
 {
   int64_t bgn = GetTimeUs();

   if (snapshot_fd != -1) {
     close(snapshot_fd);
     snapshot_fd = -1;  
   }
   
   if (snapshot_fd == -1) {
     snapshot_fd = open(g_flagmanager.m_device.c_str(), O_RDONLY, 0);

     if (snapshot_fd == -1) {
       sleep(1);
       continue;
     } else {
       //fprintf(stdout, "snapshot_fd(%d) \n", snapshot_fd);
     }
   }

   // match source ratio if possible
   if (amvideo_utils_video_playing() != 0) {
     if ( lastPriority != 255)
     {
       boblight_setpriority(boblight, 255);
       lastPriority = 255;
     }
     sleep(1);
     continue;
   }

   if (configure_capture(snapshot_fd, aml_snapshot) == 0)
   {
     if (capture_frame(snapshot_fd, aml_snapshot) == 0)
     {
       // image to boblight convert.
       frameToboblight(boblight, (uint8_t*)aml_snapshot.dst_vaddr,
       aml_snapshot.dst_width, aml_snapshot.dst_height, aml_snapshot.dst_stride);
 
       if (lastPriority != g_flagmanager.m_priority)
       {
         boblight_setpriority(boblight, g_flagmanager.m_priority);
         lastPriority = g_flagmanager.m_priority;
       }
       if (!boblight_sendrgb(boblight, 1, NULL))
       {
         // some error happened, probably connection broken, so bitch and try again
         PrintError(boblight_geterror(boblight));
         boblight_destroy(boblight);
         continue;
       }
     }
     else
     {
       fprintf(stdout, "nap time\n");
       sleep(1);
     }
   }
   int64_t end = GetTimeUs();
   float calc_time_ms = (float)(end - bgn) / 1000.0;
   // throttle to 100ms max cycle rate
   calc_time_ms -= 100.0;
   if ((int)calc_time_ms < 0)
     usleep((int)(-calc_time_ms * 1000));
 }

 // last image is black
 boblight_setpriority(boblight, 0);
 boblight_destroy(boblight);
 close(snapshot_fd);
 return 0;
}

/*********************************************************
*********************************************************/
int main(int argc, char *argv[])
{
 //load the boblight lib, if it fails we get a char* from dlerror()
 const char* boblight_error = boblight_loadlibrary(NULL);
 if (boblight_error)
 {
   PrintError(boblight_error);
   return 1;
 }

 //try to parse the flags and bitch to stderr if there's an error
 try {
   g_flagmanager.ParseFlags(argc, argv);
 }
 catch (string error) {
   PrintError(error);
   g_flagmanager.PrintHelpMessage();
   return 1;
 }
 
 if (g_flagmanager.m_printhelp) {
   g_flagmanager.PrintHelpMessage();
   return 1;
 }

 if (g_flagmanager.m_printboblightoptions) {
   g_flagmanager.PrintBoblightOptions();
   return 1;
 }
 
 // check if we only should generate a cmdline based
 // on settings from possible found boblight addon
 if (g_flagmanager.generateCmdLine)
 {
   CBoblightAddonSettings settings;
   string cmdLine = "-p 100"; //default cmdline just contains priority 100

   if (settings.m_settingsLoaded)
     cmdLine += " " + settings.getBoblightClientCmdLine();
   fprintf(stdout, "%s\n", cmdLine.c_str());
   return 0;//exit
 }

 fprintf(stderr, "Using device: %s \n", g_flagmanager.m_device.c_str());

 //set up signal handlers
 signal(SIGINT,  SignalHandler);
 signal(SIGTERM, SignalHandler);

 //keep running until we want to quit
 while(!g_stop) {
   //init boblight
   void* boblight = boblight_init();

   fprintf(stdout, "Connecting to boblightd(%p)\n", boblight);
   
   //try to connect, if we can't then bitch to stderr and destroy boblight
   if (!boblight_connect(boblight, g_flagmanager.m_address, g_flagmanager.m_port, 5000000) ||
       !boblight_setpriority(boblight, 255)) {
     PrintError(boblight_geterror(boblight));
     fprintf(stdout, "Waiting 10 seconds before trying again\n");
     boblight_destroy(boblight);
     sleep(2);
     continue;
   }

   fprintf(stdout, "Connection to boblightd opened\n");

   //try to parse the boblight flags and bitch to stderr if we can't
   try {
     g_flagmanager.ParseBoblightOptions(boblight);
   }
   catch (string error) {
     PrintError(error);
     return 1;
   }

   try {
     Run(boblight);
   }
   catch (string error) {
     PrintError(error);
     boblight_destroy(boblight);
     return 1;
   }
 }
 fprintf(stdout, "Exiting\n");
}
