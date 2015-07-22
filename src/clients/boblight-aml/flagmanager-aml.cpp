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

#include <iostream> 

#include "flagmanager-aml.h"
#include "util/misc.h"
#include "config.h"

#define DEFAULT_CAPTURE_DEVICE "/dev/amvideocap0"

using namespace std;

CFlagManagerAML::CFlagManagerAML()
{
  // extend the flags -d -> device
  // -g -> only generate cmdline from possible found boblight addon settings.xml
  m_flags += "d:g";
  m_device = DEFAULT_CAPTURE_DEVICE;
  generateCmdLine = false;
}

void CFlagManagerAML::ParseFlagsExtended(int& argc, char**& argv, int& c, char*& optarg)
{
  if (c == 'd') //devicename
  {
    if (optarg) //optional device
    {
      m_device = optarg;
    }
  }
  
  if (c == 'g') //generate cmdline
  {
    generateCmdLine = true;
  }
}

void CFlagManagerAML::PrintHelpMessage()
{
  cout << "Usage: boblight-aml\n";
  cout << "\n";
  cout << "  options:\n";
  cout << "\n";
  cout << "  -p  priority, from 0 to 255, default is 128\n";
  cout << "  -s  address[:port], set the address and optional port to connect to\n";
  cout << "  -o  add libboblight option, syntax: [light:]option=value\n";
  cout << "  -l  list libboblight options\n";
  cout << "  -f  fork\n";
  cout << "  -d  <device> (defaults to " << m_device << ")\n";
  cout << "  -g  try to find the settings.xml file from boblight addon and return the cmdline to use its options\n";
  cout << "\n";
}