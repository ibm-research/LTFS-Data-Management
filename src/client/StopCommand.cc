#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>

#include <string>
#include <list>
#include <sstream>
#include <exception>

#include "src/common/exception/OpenLTFSException.h"
#include "src/common/messages/Message.h"
#include "src/common/tracing/Trace.h"
#include "src/common/errors/errors.h"

#include "src/common/comm/ltfsdm.pb.h"
#include "src/common/comm/LTFSDmComm.h"

#include "OpenLTFSCommand.h"
#include "StopCommand.h"

void StopCommand::printUsage()
{
    INFO(LTFSDMC0007I);
}

void StopCommand::doCommand(int argc, char **argv)
{
    int lockfd;
    bool finished = false;

    processOptions(argc, argv);

    if (argc > 2) {
        printUsage();
        throw(EXCEPTION(Error::LTFSDM_GENERAL_ERROR));
    }

    try {
        connect();
    } catch (const std::exception& e) {
        MSG(LTFSDMC0026E);
        throw(EXCEPTION(Error::LTFSDM_GENERAL_ERROR));
    }

    TRACE(Trace::normal, requestNumber);

    do {
        LTFSDmProtocol::LTFSDmStopRequest *stopreq =
                commCommand.mutable_stoprequest();
        stopreq->set_key(key);
        stopreq->set_reqnumber(requestNumber);
        stopreq->set_forced(forced);
        stopreq->set_finish(false);

        try {
            commCommand.send();
        } catch (const std::exception& e) {
            MSG(LTFSDMC0027E);
            throw(EXCEPTION(Error::LTFSDM_GENERAL_ERROR));
        }

        try {
            commCommand.recv();
        } catch (const std::exception& e) {
            MSG(LTFSDMC0028E);
            throw(EXCEPTION(Error::LTFSDM_GENERAL_ERROR));
        }

        const LTFSDmProtocol::LTFSDmStopResp stopresp = commCommand.stopresp();

        finished = stopresp.success();

        if (!finished) {
            MSG(LTFSDMC0101I);
            sleep(1);
        } else {
            break;
        }
    } while (true);

    if ((lockfd = open(Const::SERVER_LOCK_FILE.c_str(), O_RDWR | O_CREAT, 0600))
            == -1) {
        MSG(LTFSDMC0033E);
        TRACE(Trace::error, Const::SERVER_LOCK_FILE, errno);
        throw(EXCEPTION(Error::LTFSDM_GENERAL_ERROR));
    }

    while (flock(lockfd, LOCK_EX | LOCK_NB) == -1) {
        if (exitClient)
            break;
        INFO(LTFSDMC0034I);
        sleep(1);
    }

    if (flock(lockfd, LOCK_UN) == -1) {
        MSG(LTFSDMC0035E);
    }
}
