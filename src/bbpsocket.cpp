// Copyright (c) 2014-2019 The Dash Core Developers, The BiblePay Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bbpsocket.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/write.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

using boost::asio::deadline_timer;
using boost::asio::ip::tcp;
using boost::lambda::bind;
using boost::lambda::var;
using boost::lambda::_1;

//----------------------------------------------------------------------

//
// This class manages socket timeouts by subclassing a deadline timer.
// Each asynchronous operation is given a deadline by which it must complete.
// Deadlines are enforced by the check_deadline actor that persists for the lifetime of the bbpsocket object:
//
//  +----------------+
//  |                |     
//  | check_deadline |<---+
//  |                |    |
//  +----------------+    | async_wait()
//              |         |
//              +---------+
//
// If the actor determines that the deadline has expired, the socket is closed
// and any outstanding operations are consequently cancelled. The socket
// operations themselves use boost::lambda function objects as completion
// handlers. For a given socket operation, the bbpsocket object runs the
// io_service to block thread execution until the actor completes.
//
// Written by Christopher M. Kohlhoff (Chris@Kohlhoff.com) and the BiblePay developers


class bbpsocket
{
public:
  bbpsocket() : socket_(io_service_), deadline_(io_service_)
  {
    deadline_.expires_at(boost::posix_time::pos_infin);
    check_deadline();
  }

  void connect(const std::string& host, const std::string& service,
      boost::posix_time::time_duration timeout)
  {
    tcp::resolver::query query(host, service);
    tcp::resolver::iterator iter = tcp::resolver(io_service_).resolve(query);

    // Set a deadline for the asynchronous operation. 
    deadline_.expires_from_now(timeout);
    boost::system::error_code ec = boost::asio::error::would_block;

    // Start the asynchronous operation itself. The boost::lambda function
    // object is used as a callback and will update the ec when the operation completes. 
    boost::asio::async_connect(socket_, iter, var(ec) = boost::lambda::_1);

    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec == boost::asio::error::would_block);
    if (ec || !socket_.is_open())
      throw boost::system::system_error(
          ec ? ec : boost::asio::error::operation_aborted);
  }

  std::string read_line(boost::posix_time::time_duration timeout)
  {
    // Set a deadline
    deadline_.expires_from_now(timeout);
    boost::system::error_code ec = boost::asio::error::would_block;
    boost::asio::async_read_until(socket_, input_buffer_, '\n', var(ec) = boost::lambda::_1);
    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec == boost::asio::error::would_block);

    if (ec)
      throw boost::system::system_error(ec);

    std::string line;
    std::istream is(&input_buffer_);
    std::getline(is, line);
    return line;
  }

  void write_line(const std::string& line,
      boost::posix_time::time_duration timeout)
  {
    std::string data = line + "\n";
    // Set a deadline
    deadline_.expires_from_now(timeout);
    boost::system::error_code ec = boost::asio::error::would_block;
    boost::asio::async_write(socket_, boost::asio::buffer(data), var(ec) = boost::lambda::_1);
    // Block until the asynchronous operation has completed.
    do io_service_.run_one(); while (ec == boost::asio::error::would_block);
    if (ec)
      throw boost::system::system_error(ec);
  }

private:
  void check_deadline()
  {
    if (deadline_.expires_at() <= deadline_timer::traits_type::now())
    {
      // The deadline has passed. 
      boost::system::error_code ignored_ec;
      socket_.close(ignored_ec);
      deadline_.expires_at(boost::posix_time::pos_infin);
    }
    // Put the actor back to sleep.
    deadline_.async_wait(bind(&bbpsocket::check_deadline, this));
  }

  boost::asio::io_service io_service_;
  tcp::socket socket_;
  deadline_timer deadline_;
  boost::asio::streambuf input_buffer_;
};

//----------------------------------------------------------------------

std::string sPrepareVersion()
{
	ServiceFlags nLocalNodeServices = g_connman->GetLocalServices();
	CAddress addrYou = CAddress(CService(), nLocalNodeServices);
	CAddress addrMe = CAddress(CService(), nLocalNodeServices);
	uint256 mnauthChallenge;
	GetRandBytes(mnauthChallenge.begin(), mnauthChallenge.size());
	int64_t nTime = GetAdjustedTime();
	CSerializedNetMsg msgmversion = CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERSION, PROTOCOL_VERSION, (uint64_t)nLocalNodeServices, nTime, addrYou, addrMe,
            1, strSubVersion, 0, ::fRelayTxes, mnauthChallenge);
	std::string sVersion = VectorToString(msgmversion.data);
	return sVersion;
}

std::string PrepareHTTPPost(bool bPost, std::string sPage, std::string sHostHeader, const std::string& sMsg, const std::map<std::string,std::string>& mapRequestHeaders)
{
	std::ostringstream s;
	std::string sUserAgent = "Mozilla/5.0";
	std::string sMethod = bPost ? "POST" : "GET";

	s << sMethod + " /" + sPage + " HTTP/1.1\r\n"
		<< "User-Agent: " + sUserAgent + "/" << FormatFullVersion() << "\r\n"
		<< "Host: " + sHostHeader + "" << "\r\n"
		<< "Content-Length: " << sMsg.size() << "\r\n";

	for (auto item : mapRequestHeaders) 
	{
        s << item.first << ": " << item.second << "\r\n";
	}
    s << "\r\n" << sMsg;
    return s.str();
}

std::string BBPPost(std::string sHost, std::string sService, std::string sPage, std::string sPayload, int iTimeout)
{
	std::string sData;
	try
	{
		bbpsocket c;
		c.connect(sHost, sService, boost::posix_time::seconds(iTimeout));
		boost::posix_time::ptime time_sent = boost::posix_time::microsec_clock::universal_time();
		std::map<std::string, std::string> mapRequestHeaders;
		mapRequestHeaders["Agent"] = FormatFullVersion();
		std::string sPost = PrepareHTTPPost(true, sPage, sHost, sPayload, mapRequestHeaders);
		c.write_line(sPost, boost::posix_time::seconds(iTimeout));
		for (;;)
		{
		  std::string line = c.read_line(boost::posix_time::seconds(iTimeout));
  		  sData += line;
	  	  if (Contains(line, "</html>") || Contains(line,"<eof>") || Contains(line,"<END>"))
			  break;
		}
  }
  catch (std::exception& e)
  {
      std::string sErr = std::string("BBPPostException::") + e.what();
	  return sErr;
  }
  return sData;
}

	
    