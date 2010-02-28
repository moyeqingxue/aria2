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
#include "PeerAbstractCommand.h"
#include "Peer.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "DlAbortEx.h"
#include "Socket.h"
#include "Logger.h"
#include "message.h"
#include "prefs.h"
#include "DownloadFailureException.h"
#include "StringFormat.h"

namespace aria2 {

PeerAbstractCommand::PeerAbstractCommand(int32_t cuid,
                                         const SharedHandle<Peer>& peer,
                                         DownloadEngine* e,
                                         const SocketHandle& s):
  Command(cuid),
  e(e),
  socket(s),
  peer(peer),
  checkSocketIsReadable(false),
  checkSocketIsWritable(false),
  noCheck(false)
{
  if(!socket.isNull() && socket->isOpen()) {
    setReadCheckSocket(socket);
  }
  // TODO referring global option
  timeout = e->option->getAsInt(PREF_BT_TIMEOUT);
}

PeerAbstractCommand::~PeerAbstractCommand()
{
  disableReadCheckSocket();
  disableWriteCheckSocket();
}

bool PeerAbstractCommand::execute()
{
  if(logger->debug()) {
    logger->debug("CUID#%d -"
                  " socket: read:%d, write:%d, hup:%d, err:%d, noCheck:%d",
                  cuid, _readEvent, _writeEvent, _hupEvent, _errorEvent,
                  noCheck);
  }
  if(exitBeforeExecute()) {
    onAbort();
    return true;
  }
  try {
    if(noCheck ||
       (checkSocketIsReadable && _readEvent) ||
       (checkSocketIsWritable && _writeEvent) ||
       _hupEvent) {
      checkPoint.reset();
    } else if(_errorEvent) {
      throw DL_ABORT_EX
        (StringFormat(MSG_NETWORK_PROBLEM,
                      socket->getSocketError().c_str()).str());
    }
    if(checkPoint.elapsed(timeout)) {
      throw DL_ABORT_EX(EX_TIME_OUT);
    }
    return executeInternal();
  } catch(DownloadFailureException& err) {
    logger->error(EX_DOWNLOAD_ABORTED, err);
    onAbort();
    onFailure();
    return true;
  } catch(RecoverableException& err) {
    if(logger->debug()) {
      logger->debug(MSG_TORRENT_DOWNLOAD_ABORTED, err, cuid);
      logger->debug(MSG_PEER_BANNED,
                    cuid, peer->ipaddr.c_str(), peer->port);
    }
    onAbort();
    return prepareForNextPeer(0);
  }
}

// TODO this method removed when PeerBalancerCommand is implemented
bool PeerAbstractCommand::prepareForNextPeer(time_t wait)
{
  return true;
}

void PeerAbstractCommand::disableReadCheckSocket()
{
  if(checkSocketIsReadable) {
    e->deleteSocketForReadCheck(readCheckTarget, this);
    checkSocketIsReadable = false;
    readCheckTarget = SocketHandle();
  }  
}

void PeerAbstractCommand::setReadCheckSocket(const SocketHandle& socket)
{
  if(!socket->isOpen()) {
    disableReadCheckSocket();
  } else {
    if(checkSocketIsReadable) {
      if(readCheckTarget != socket) {
        e->deleteSocketForReadCheck(readCheckTarget, this);
        e->addSocketForReadCheck(socket, this);
        readCheckTarget = socket;
      }
    } else {
      e->addSocketForReadCheck(socket, this);
      checkSocketIsReadable = true;
      readCheckTarget = socket;
    }
  }
}

void PeerAbstractCommand::disableWriteCheckSocket()
{
  if(checkSocketIsWritable) {
    e->deleteSocketForWriteCheck(writeCheckTarget, this);
    checkSocketIsWritable = false;
    writeCheckTarget = SocketHandle();
  }
}

void PeerAbstractCommand::setWriteCheckSocket(const SocketHandle& socket)
{
  if(!socket->isOpen()) {
    disableWriteCheckSocket();
  } else {
    if(checkSocketIsWritable) {
      if(writeCheckTarget != socket) {
        e->deleteSocketForWriteCheck(writeCheckTarget, this);
        e->addSocketForWriteCheck(socket, this);
        writeCheckTarget = socket;
      }
    } else {
      e->addSocketForWriteCheck(socket, this);
      checkSocketIsWritable = true;
      writeCheckTarget = socket;
    }
  }
}

void PeerAbstractCommand::setNoCheck(bool check)
{
  this->noCheck = check;
}

void PeerAbstractCommand::updateKeepAlive()
{
  checkPoint.reset();
}

} // namespace aria2
