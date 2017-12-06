#pragma once

#include <string.h>
#include <atomic>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <mutex>

#include "boost/format.hpp"

#include "msgdefs.h"
#include "src/common/errors/errors.h"
#include "src/common/const/Const.h"

/**
    @page messaging_system Messaging

    # Messaging System

    OpenLTFS writes output to the console and a log file. Every output to these
    two locations should be regarded as a message even it is a single character.
    Tracing does not used messages since only values of variables are printed
    out. All messages are consolidated in a single file
    <a href="../messages.cfg">messages.cfg</a> within the root of the code tree.

    There are two types of messages:

    - Informational messages that do not show up the message identifier.
      Those messages are added to the code using the INFO() macro.
    - Messages that should show up the message identifier. For those
      the MSG() macro should be used.

    The INFO() macro is especially used for the output of the client commands.

    The  <a href="../messages.cfg">messages.cfg</a> has a special format:

    - Empty lines are allowed.
    - A '#' character at the beginning of a line indicates a comment.
    - Usually a message starts with a message identifier followed by the message
      surrounded by quotes.
    - If the message identifier is missing the message text is added to the
      previous message.

    The message itself has to be written in c printf style format. E.g.:

    @code
    LTFSDMX0001E "Unable to setup tracing: %d.\n"
    @endcode

    The message identifier is assembled in the following way:

    @code
    LTFSDM[X|C|S|D|F|L]NNNN[I|E|W]
    @endcode

    in which the different characters have to following meaning:

    characters | meaning
    :---:|---
    X | common message used by different parts of the code (client, server, ...)
    C | a client message
    S | a server message
    D | a message used by the dmapi connector
    F | a message used by the Fuse connector
    L | a message used by LTFS
    NNNN | a four digit number
    I | an informational message
    E | an error message
    W | a warning

    A line feed not automatically is added. It is necessary to add a "\n" sequence
    if required.

    There is a message compiler @ref msgcompiler.cc that
    transforms this text based  <a href="../messages.cfg">messages.cfg</a>
    message file into c++ code. This operation is done at the beginning of
    the build process. I.e. some development environment may show up errors
    that some symbols could not be resolved before building the code.

    The macros MSG() and INFO() are used here to automatically add the file
    name and the line number to the output. The class Message is responsible
    to process the message string and corresponding arguments. Internally
    the <a href="http://www.boost.org/doc/libs/release/libs/format/">Boost Format library</a>
    is used to perform the formatting.

    A messaging object @ref messageObject is created for message processing. It
    should not be used directly but as part of the MSG() and INFO() macros.

    The following gives an overview about the internal processing of a message:

    @dot
    digraph message {
        fontname="fixed";
        fontsize=11;
        labeljust=l;
        node [shape=record, width=2, fontname="fixed", fontsize=11, fillcolor=white, style=filled];
        msg [ fontname="fixed bold", fontcolor=dodgerblue4, label="MSG()", URL="@ref MSG()" ];
        message [ fontname="fixed bold", fontcolor=dodgerblue4, label="messageObject.message", URL="@ref Message::message" ];
        condition [shape=diamond, label="stdout?"];
        process_parms [ fontname="fixed bold", fontcolor=dodgerblue4, label="processParms", URL="@ref Message::processParms" ];
        subgraph cluster_to_log {
            label="to log file";
            msg_log [ fontname="fixed bold", fontcolor=dodgerblue4, label="msgLog", URL="@ref Message::msgLog"];
            write_log [ fontname="fixed bold", fontcolor=dodgerblue4, label="writeLog", URL="@ref Message::writeLog"];
            result_log [ label="write to log" ];
        }
        subgraph cluster_to_stdout {
            label="to stdout";
            msg_out [ fontname="fixed bold", fontcolor=dodgerblue4, label="msgOut", URL="@ref Message::msgOut"];
            write_out [ fontname="fixed bold", fontcolor=dodgerblue4, label="writeOut", URL="@ref Message::writeOut"];
            result_out [ label="write to stdout" ];
        }
        msg -> message [];
        message -> condition [];
        condition -> msg_log [label="no"];
        condition -> msg_out [label="yes"];
        msg_log -> process_parms [];
        msg_out -> process_parms [];
        process_parms -> process_parms [];
        process_parms -> write_log [label="boost::format fmter"];
        process_parms -> write_out [label="boost::format fmter"];
        msg_log -> write_log [style=invis];
        msg_out -> write_out [style=invis];
        write_log -> result_log [];
        write_out -> result_out [];
    }
    @enddot

 */

class Message
{
private:
    std::mutex mtx;
    int fd;
    std::string fileName;
public:
    enum LogType
    {
        STDOUT, LOGFILE
    };
private:
    std::atomic<Message::LogType> logType;

    inline void processParms(boost::format *fmter)
    {
    }
    template<typename T>
    void processParms(boost::format *fmter, T s)
    {
        *fmter % s;
    }
    template<typename T, typename ... Args>
    void processParms(boost::format *fmter, T s, Args ... args)
    {
        *fmter % s;
        processParms(fmter, args ...);
    }

    void writeOut(std::string msgstr);
    void writeLog(std::string msgstr);

    template<typename ... Args>
    void msgOut(msg_id msg, char *filename, int linenr, Args ... args)

    {
        std::string fmtstr = msgname[msg] + "(%04d): " + messages[msg];
        boost::format fmter(fmtstr);
        fmter.exceptions(boost::io::all_error_bits);

        try {
            fmter % linenr;
            processParms(&fmter, args ...);
            writeOut(fmter.str());
        } catch (const std::exception& e) {
            std::cerr << messages[LTFSDMX0005E] << " (" << msgname[msg] << ":"
                    << filename << ":" << std::setfill('0') << std::setw(4)
                    << linenr << ")" << std::endl;
        }
    }
    template<typename ... Args>
    void msgLog(msg_id msg, char *filename, int linenr, Args ... args)
    {
        std::string fmtstr = msgname[msg] + "(%04d): " + messages[msg];
        boost::format fmter(fmtstr);
        fmter.exceptions(boost::io::all_error_bits);

        try {
            fmter % linenr;
            processParms(&fmter, args ...);
            writeLog(fmter.str());
        } catch (const std::exception& e) {
            std::cerr << messages[LTFSDMX0005E] << " (" << msgname[msg] << ":"
                    << filename << ":" << std::setfill('0') << std::setw(4)
                    << linenr << ")" << std::endl;
        }
    }

public:
    Message() :
            fd(Const::UNSET), fileName(Const::LOG_FILE), logType(
                    Message::STDOUT)
    {
    }
    ~Message();

    void init(std::string extension = "");

    void setLogType(Message::LogType type)
    {
        logType = type;
    }

    Message::LogType getLogType()
    {
        return logType;
    }

    template<typename ... Args>
    void message(msg_id msg, char *filename, int linenr, Args ... args)
    {
        if (logType == Message::STDOUT)
            msgOut(msg, filename, linenr, args ...);
        else
            msgLog(msg, filename, linenr, args ...);
    }

    template<typename ... Args>
    void info(msg_id msg, char *filename, int linenr, Args ... args)
    {
        boost::format fmter(messages[msg]);
        fmter.exceptions(boost::io::all_error_bits);

        try {
            processParms(&fmter, args ...);
            writeOut(fmter.str());
        } catch (const std::exception& e) {
            std::cerr << messages[LTFSDMX0005E] << " (" << filename << ":"
                    << std::setfill('0') << std::setw(4) << linenr << ")"
                    << std::endl;
            exit((int) Error::GENERAL_ERROR);
        }
    }
};

extern Message messageObject;

#define MSG(msg, args ...) messageObject.message(msg, (char *) __FILE__, __LINE__, ##args)
#define INFO(msg, args ...) messageObject.info(msg, (char *) __FILE__, __LINE__, ##args)
