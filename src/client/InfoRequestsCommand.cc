/*******************************************************************************
 * Copyright 2018 IBM Corp. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *******************************************************************************/
#include <sys/resource.h>

#include <unistd.h>
#include <string>
#include <list>
#include <sstream>
#include <exception>

#include "src/common/errors.h"
#include "src/common/LTFSDMException.h"
#include "src/common/Message.h"
#include "src/common/Trace.h"

#include "src/communication/ltfsdm.pb.h"
#include "src/communication/LTFSDmComm.h"

#include "LTFSDMCommand.h"
#include "InfoRequestsCommand.h"

/** @page ltfsdm_info_requests ltfsdm info requests
    The ltfsdm info request command lists all LTFS Data Management requests
    and their corresponding status.

    <tt>@LTFSDMC0009I</tt>

    parameters | description
    ---|---
    -n \<request number\> | request number for a specific request to see the information

    Example:

    @verbatim
    [root@visp ~]# ltfsdm info requests -n 28
    operation            state                request number       tape pool            tape id              target state
    migration            in progress          28                   pool1                D01301L5             in progress
    @endverbatim

    The corresponding class is @ref InfoRequestsCommand.
 */

void InfoRequestsCommand::printUsage()
{
    INFO(LTFSDMC0009I);
}

void InfoRequestsCommand::doCommand(int argc, char **argv)
{
    long reqOfInterest;

    processOptions(argc, argv);

    TRACE(Trace::normal, *argv, argc, optind);

    if (argc != optind) {
        printUsage();
        THROW(Error::GENERAL_ERROR);
    } else if (requestNumber < Const::UNSET) {
        printUsage();
        THROW(Error::GENERAL_ERROR);
    }

    reqOfInterest = requestNumber;

    try {
        connect();
    } catch (const std::exception& e) {
        MSG(LTFSDMC0026E);
        return;
    }

    LTFSDmProtocol::LTFSDmInfoRequestsRequest *inforeqs =
            commCommand.mutable_inforequestsrequest();

    inforeqs->set_key(key);
    inforeqs->set_reqnumber(reqOfInterest);

    try {
        commCommand.send();
    } catch (const std::exception& e) {
        MSG(LTFSDMC0027E);
        THROW(Error::GENERAL_ERROR);
    }

    INFO(LTFSDMC0060I);
    int recnum;

    do {
        try {
            commCommand.recv();
        } catch (const std::exception& e) {
            MSG(LTFSDMC0028E);
            THROW(Error::GENERAL_ERROR);
        }

        const LTFSDmProtocol::LTFSDmInfoRequestsResp inforeqsresp =
                commCommand.inforequestsresp();
        std::string operation = inforeqsresp.operation();
        recnum = inforeqsresp.reqnumber();
        std::string tapeid = inforeqsresp.tapeid();
        std::string tstate = inforeqsresp.targetstate();
        std::string state = inforeqsresp.state();
        std::string pool = inforeqsresp.pool();
        if (recnum != Const::UNSET)
            INFO(LTFSDMC0061I, operation, state, recnum, pool, tapeid, tstate);

    } while (recnum != Const::UNSET);

    return;
}
