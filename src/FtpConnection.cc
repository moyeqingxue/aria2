/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "FtpConnection.h"

#include <cstring>
#include <cstdio>
#include <cassert>

#include "Request.h"
#include "Segment.h"
#include "Option.h"
#include "util.h"
#include "message.h"
#include "prefs.h"
#include "LogFactory.h"
#include "Logger.h"
#include "AuthConfigFactory.h"
#include "AuthConfig.h"
#include "DlRetryEx.h"
#include "DlAbortEx.h"
#include "Socket.h"
#include "A2STR.h"
#include "StringFormat.h"
#include "AuthConfig.h"
#include "a2functional.h"

namespace aria2 {

const std::string FtpConnection::A("A");

const std::string FtpConnection::I("I");

FtpConnection::FtpConnection(int32_t cuid, const SocketHandle& socket,
                             const SharedHandle<Request>& req,
                             const SharedHandle<AuthConfig>& authConfig,
                             const Option* op):
  cuid(cuid), socket(socket), req(req),
  _authConfig(authConfig), option(op),
  logger(LogFactory::getInstance()),
  _socketBuffer(socket),
  _baseWorkingDir("/") {}

FtpConnection::~FtpConnection() {}

bool FtpConnection::sendUser()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "USER ";
    request += _authConfig->getUser();
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, "USER ********");
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendPass()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "PASS ";
    request += _authConfig->getPassword();
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, "PASS ********");
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendType()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string type;
    if(option->get(PREF_FTP_TYPE) == V_ASCII) {
      type = FtpConnection::A;
    } else {
      type = FtpConnection::I;
    }
    std::string request = "TYPE ";
    request += type;
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendPwd()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "PWD\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendCwd()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    logger->info("CUID#%d - Using base working directory '%s'",
                 cuid, _baseWorkingDir.c_str());
    std::string request = "CWD ";
    if(_baseWorkingDir != "/") {
      request += _baseWorkingDir;
    }
    request += util::urldecode(req->getDir());
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendMdtm()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "MDTM ";
    request += util::urlencode(req->getFile());
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendSize()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "SIZE ";
    request += util::urldecode(req->getFile());
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendPasv()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    static const std::string request("PASV\r\n");
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

SharedHandle<SocketCore> FtpConnection::createServerSocket()
{
  SharedHandle<SocketCore> serverSocket(new SocketCore());
  serverSocket->bind(0);
  serverSocket->beginListen();
  serverSocket->setNonBlockingMode();
  return serverSocket;
}

bool FtpConnection::sendPort(const SharedHandle<SocketCore>& serverSocket)
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::pair<std::string, uint16_t> addrinfo;
    socket->getAddrInfo(addrinfo);
    unsigned int ipaddr[4]; 
    sscanf(addrinfo.first.c_str(), "%u.%u.%u.%u",
           &ipaddr[0], &ipaddr[1], &ipaddr[2], &ipaddr[3]);
    serverSocket->getAddrInfo(addrinfo);
    std::string request = "PORT ";
    request += util::uitos(ipaddr[0]);
    request += ",";
    request += util::uitos(ipaddr[1]);
    request += ",";
    request += util::uitos(ipaddr[2]);
    request += ",";
    request += util::uitos(ipaddr[3]);
    request += ",";
    request += util::uitos(addrinfo.second/256);
    request += ",";
    request += util::uitos(addrinfo.second%256);
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendRest(const SharedHandle<Segment>& segment)
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "REST ";
    if(segment.isNull()) {
      request += "0";
    } else {
      request += util::itos(segment->getPositionToWrite());
    }
    request += "\r\n";
    
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

bool FtpConnection::sendRetr()
{
  if(_socketBuffer.sendBufferIsEmpty()) {
    std::string request = "RETR ";
    request += util::urldecode(req->getFile());
    request += "\r\n";
    logger->info(MSG_SENDING_REQUEST, cuid, request.c_str());
    _socketBuffer.feedSendBuffer(request);
  }
  _socketBuffer.send();
  return _socketBuffer.sendBufferIsEmpty();
}

unsigned int FtpConnection::getStatus(const std::string& response) const
{
  unsigned int status;
  // When the response is not like "%u %*s",
  // we return 0.
  if(response.find_first_not_of("0123456789") != 3
     || !(response.find(" ") == 3 || response.find("-") == 3)) {
    return 0;
  }
  if(sscanf(response.c_str(), "%u %*s", &status) == 1) {
    return status;
  } else {
    return 0;
  }
}

// Returns the length of the reponse if the whole response has been received.
// The length includes \r\n.
// If the whole response has not been received, then returns std::string::npos.
std::string::size_type
FtpConnection::findEndOfResponse(unsigned int status,
                                 const std::string& buf) const
{
  if(buf.size() <= 4) {
    return std::string::npos;
  }
  // if 4th character of buf is '-', then multi line response is expected.
  if(buf.at(3) == '-') {
    // multi line response
    std::string::size_type p;

    std::string endPattern = A2STR::CRLF;
    endPattern += util::uitos(status);
    endPattern += " ";
    p = buf.find(endPattern);
    if(p == std::string::npos) {
      return std::string::npos;
    }
    p = buf.find(A2STR::CRLF, p+6);
    if(p == std::string::npos) {
      return std::string::npos;
    } else {
      return p+2;
    }
  } else {
    // single line response
    std::string::size_type p = buf.find(A2STR::CRLF);    
    if(p == std::string::npos) {
      return std::string::npos;
    } else {
      return p+2;
    }
  }
}

bool FtpConnection::bulkReceiveResponse(std::pair<unsigned int, std::string>& response)
{
  char buf[1024];  
  while(socket->isReadable(0)) {
    size_t size = sizeof(buf);
    socket->readData(buf, size);
    if(size == 0) {
      if(socket->wantRead() || socket->wantWrite()) {
        return false;
      }
      throw DL_RETRY_EX(EX_GOT_EOF);
    }
    if(strbuf.size()+size > MAX_RECV_BUFFER) {
      throw DL_RETRY_EX
        (StringFormat("Max FTP recv buffer reached. length=%lu",
                      static_cast<unsigned long>(strbuf.size()+size)).str());
    }
    strbuf.append(&buf[0], &buf[size]);
  }
  unsigned int status;
  if(strbuf.size() >= 4) {
    status = getStatus(strbuf);
    if(status == 0) {
      throw DL_ABORT_EX(EX_INVALID_RESPONSE);
    }
  } else {
    return false;
  }
  std::string::size_type length;
  if((length = findEndOfResponse(status, strbuf)) != std::string::npos) {
    response.first = status;
    response.second = strbuf.substr(0, length);
    logger->info(MSG_RECEIVE_RESPONSE, cuid, response.second.c_str());
    strbuf.erase(0, length);
    return true;
  } else {
    // didn't receive response fully.
    return false;
  }
}

unsigned int FtpConnection::receiveResponse()
{
  std::pair<unsigned int, std::string> response;
  if(bulkReceiveResponse(response)) {
    return response.first;
  } else {
    return 0;
  }
}

#ifdef __MINGW32__
# define LONGLONG_PRINTF "%I64d"
# define ULONGLONG_PRINTF "%I64u"
# define LONGLONG_SCANF "%I64d"
# define ULONGLONG_SCANF "%I64u"
#else
# define LONGLONG_PRINTF "%lld"
# define ULONGLONG_PRINTF "%llu"
# define LONGLONG_SCANF "%Ld"
// Mac OSX uses "%llu" for 64bits integer.
# define ULONGLONG_SCANF "%Lu"
#endif // __MINGW32__

unsigned int FtpConnection::receiveSizeResponse(uint64_t& size)
{
  std::pair<unsigned int, std::string> response;
  if(bulkReceiveResponse(response)) {
    if(response.first == 213) {
      std::pair<std::string, std::string> rp = util::split(response.second," ");
      size = util::parseULLInt(rp.second);
    }
    return response.first;
  } else {
    return 0;
  }
}

unsigned int FtpConnection::receiveMdtmResponse(Time& time)
{
  // MDTM command, specified in RFC3659.
  std::pair<unsigned int, std::string> response;
  if(bulkReceiveResponse(response)) {
    if(response.first == 213) {
      char buf[15]; // YYYYMMDDhhmmss+\0, milli second part is dropped.
      sscanf(response.second.c_str(), "%*u %14s", buf);
      if(strlen(buf) == 14) {
        // We don't use Time::parse(buf,"%Y%m%d%H%M%S") here because Mac OS X
        // and included strptime doesn't parse data for this format.
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_sec = util::parseInt(&buf[12]);
        buf[12] = '\0';
        tm.tm_min = util::parseInt(&buf[10]);
        buf[10] = '\0';
        tm.tm_hour = util::parseInt(&buf[8]);
        buf[8] = '\0';
        tm.tm_mday = util::parseInt(&buf[6]);
        buf[6] = '\0';
        tm.tm_mon = util::parseInt(&buf[4])-1;
        buf[4] = '\0';
        tm.tm_year = util::parseInt(&buf[0])-1900;
        time = Time(timegm(&tm));
      } else {
        time = Time::null();
      }
    }
    return response.first;
  } else {
    return 0;
  }
}

unsigned int FtpConnection::receivePasvResponse(std::pair<std::string, uint16_t>& dest)
{
  std::pair<unsigned int, std::string> response;
  if(bulkReceiveResponse(response)) {
    if(response.first == 227) {
      // we assume the format of response is "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)."
      unsigned int h1, h2, h3, h4, p1, p2;
      std::string::size_type p = response.second.find("(");
      if(p >= 4) {
        sscanf(response.second.substr(response.second.find("(")).c_str(),
               "(%u,%u,%u,%u,%u,%u).",
               &h1, &h2, &h3, &h4, &p1, &p2);
        // ip address
        dest.first = util::uitos(h1);
        dest.first += A2STR::DOT_C;
        dest.first += util::uitos(h2);
        dest.first += A2STR::DOT_C;
        dest.first += util::uitos(h3);
        dest.first += A2STR::DOT_C;
        dest.first += util::uitos(h4);
        // port number
        dest.second = 256*p1+p2;
      } else {
        throw DL_RETRY_EX(EX_INVALID_RESPONSE);
      }
    }
    return response.first;
  } else {
    return 0;
  }
}

unsigned int FtpConnection::receivePwdResponse(std::string& pwd)
{
  std::pair<unsigned int, std::string> response;
  if(bulkReceiveResponse(response)) {
    if(response.first == 257) {
      std::string::size_type first;
      std::string::size_type last;

      if((first = response.second.find("\"")) != std::string::npos &&
         (last = response.second.find("\"", ++first)) != std::string::npos) {
        pwd = response.second.substr(first, last-first);
      } else {
        throw DL_ABORT_EX(EX_INVALID_RESPONSE);
      }
    }
    return response.first;
  } else {
    return 0;
  }
}

void FtpConnection::setBaseWorkingDir(const std::string& baseWorkingDir)
{
  _baseWorkingDir = baseWorkingDir;
}

} // namespace aria2
