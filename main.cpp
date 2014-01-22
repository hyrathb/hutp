#include <cstdlib>
#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;



class tcp_connection
    : public boost::enable_shared_from_this<tcp_connection>
{
public:
    typedef boost::shared_ptr<tcp_connection> pointer;
    typedef boost::shared_ptr<boost::asio::deadline_timer> ptimer;

    static pointer create(boost::asio::io_service& io_service)
    {
        return pointer(new tcp_connection(io_service));
    }

    tcp::socket& socket()
    {
        return socket_;
    }

    void start()
    {
        std::cout << "TCP:" << socket_.remote_endpoint().address() << " connected" << std::endl;
        socket_.async_read_some(boost::asio::buffer(req_buf_), boost::bind(&tcp_connection::handle_read1, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    }

private:
    tcp_connection(boost::asio::io_service& io_service)
        : socket__(io_service), resolver_(io_service), socket_(io_service), linked_(0)
    {
        memset(req_buf_, 0, sizeof(req_buf_));
        memset(res_buf_, 0, sizeof(res_buf_));
    }

    void handle_read1(const boost::system::error_code& ec, size_t byte_transferred)
    {
        if (byte_transferred > 0)
        {
            if (get_target(url_, port_))
            {
                std::cout << url_ << " " << port_ << std::endl;
                boost::asio::ip::tcp::resolver::query query(url_, "http");
                resolver_.async_resolve(query, boost::bind(&tcp_connection::handle_resolve, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::iterator, 0));

            }
            else
            {
                socket__.async_write_some(boost::asio::buffer(req_buf_, byte_transferred), boost::bind(&tcp_connection::handle_write, shared_from_this(), boost::asio::placeholders::error));
            }
            socket_.async_read_some(boost::asio::buffer(req_buf_), boost::bind(&tcp_connection::handle_read1, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

        }


    }

    void handle_resolve(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpoint_iterator, int  recount)
    {
        if (!ec)
        {
            if (!socket__.is_open())
            {
                endpoint_= *endpoint_iterator;
                endpoint_.port(port_);
                socket__.async_connect(endpoint_, boost::bind(&tcp_connection::handle_connect, shared_from_this(), boost::asio::placeholders::error));
            }
            else
            {
                if (recount >= 15)
                {
                    socket__.close();
                    endpoint_= *endpoint_iterator;
                    endpoint_.port(port_);
                    socket__.async_connect(endpoint_, boost::bind(&tcp_connection::handle_connect, shared_from_this(), boost::asio::placeholders::error));
                }
                else
                {
                    ptimer timer_(new boost::asio::deadline_timer(resolver_.get_io_service()));
                    timer_->expires_from_now(boost::posix_time::seconds(1));
                    timer_->async_wait(boost::bind(&tcp_connection::handle_resolve, shared_from_this(), ec, endpoint_iterator, recount+1));
                }
            }
        }
        else
            std::cerr << "Resolve Error:" << boost::system::error_code(ec) << std::endl;
    }


    void handle_connect(const boost::system::error_code& ec)
    {
        if (!ec)
        {
            linked_= 1;
            socket__.async_write_some(boost::asio::buffer(req_buf_), boost::bind(&tcp_connection::handle_write, shared_from_this(), boost::asio::placeholders::error));
            socket__.async_read_some(boost::asio::buffer(res_buf_), boost::bind(&tcp_connection::handle_read2, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        }
        else
            std::cerr << "Connect to server Error:" << boost::system::error_code(ec) << std::endl;
    }

    void handle_write(const boost::system::error_code& ec)
    {

    }

    void handle_read2(const boost::system::error_code& ec, size_t byte_transferred)
    {
        if (byte_transferred > 0)
        {
            socket_.async_write_some(boost::asio::buffer(res_buf_, byte_transferred), boost::bind(&tcp_connection::handle_write2, shared_from_this()));
            socket__.async_read_some(boost::asio::buffer(res_buf_), boost::bind(&tcp_connection::handle_read2, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        }
    }
    int get_target(std::string &url, short &port)
    {
        url= "";
        port= 0;
        char * tmp= req_buf_;
        tmp=std::strstr(req_buf_, "Host: ");
        if (tmp)
        {
            tmp+=6;
            while((*tmp != ' ') && (*tmp != '\n') && (*tmp != '\t') && (*tmp != '\r') && (*tmp != '\0') && (*tmp!=':'))
            {
                url += *tmp;
                ++tmp;
            }
            if(*tmp== ':')
            {
                while((*tmp!= ' ') && (*tmp!= '\n') && (*tmp != '\t') && (*tmp != '\r') && (*tmp != '\0') && (*tmp != ':'))
                {
                    port =port*10+ *tmp-'0';
                    ++tmp;
                }
            }
            else
            {
                port= 80;
            }

        }
        return url.length();
    }
    void handle_write2()
    {
    }


    tcp::resolver resolver_;
    tcp::socket socket__;
    tcp::socket socket_;
    int linked_;
    short port_;
    std::string url_;
    char req_buf_[81920];
    char res_buf_[81920];
    //boost::asio::deadline_timer timer_;
    boost::asio::ip::tcp::endpoint endpoint_;

};

class tcp_server
{
public:
    tcp_server(boost::asio::io_service& io_service, int port)
        : acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
    {
        start_accept();
    }

private:
    void start_accept()
    {
        tcp_connection::pointer new_connection =
            tcp_connection::create(acceptor_.get_io_service());

        acceptor_.async_accept(new_connection->socket(),
                               boost::bind(&tcp_server::handle_accept, this, new_connection,
                                           boost::asio::placeholders::error));
    }

    void handle_accept(tcp_connection::pointer new_connection,
                       const boost::system::error_code& error)
    {
        if (!error)
        {
            new_connection->start();
            start_accept();
        }
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char ** argv)
{
    if (argc!=2)
    {
        std::cout << "Usage: command [port]" << std::endl;
        return 0;
    }
    try
    {
        boost::asio::io_service io_service;
        tcp_server server1(io_service, atoi(argv[1]));
        io_service.run();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
