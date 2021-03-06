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
#include "ServerIncludes.h"

std::atomic<bool> Server::terminate;
std::atomic<bool> Server::forcedTerminate;
std::atomic<bool> Server::finishTerminate;
std::mutex Server::termmtx;
std::condition_variable Server::termcond;
Configuration Server::conf;

ThreadPool<Migration::mig_info_t, std::shared_ptr<std::list<unsigned long>>,
        FsObj::file_state> *Server::wqs;

int Server::statTapeRetry(std::string tapeId, const char *pathname,
        struct stat *buf)

{
    int rc;
    int retry = 0;

    while ((rc = stat(pathname, buf)) == -1 && errno == EBUSY && retry < 10) {
        TRACE(Trace::error, pathname);
        retry++;
        sleep(1);
    }

    if (rc == -1 && errno == EBUSY)
        inventory->updateCartridge(tapeId);

    return rc;
}

int Server::openTapeRetry(std::string tapeId, const char *pathname, int flags)

{
    int rc;
    int retry = 0;

    while ((rc = open(pathname, flags)) == -1 && errno == EBUSY && retry < 10) {
        TRACE(Trace::error, pathname);
        retry++;
        sleep(1);
    }

    if (rc == -1 && errno == EBUSY)
        inventory->updateCartridge(tapeId);

    return rc;
}

std::string Server::getTapeName(FsObj *diskFile, std::string tapeId)

{
    std::stringstream tapeName;
    fuid_t fuid = diskFile->getfuid();

    tapeName << inventory->getMountPoint() << Const::DELIM << tapeId
            << Const::DELIM << Const::LTFSDM_DATA_DIR << Const::DELIM
            << Const::LTFS_NAME << "." << fuid.fsid_h << "." << fuid.fsid_l
            << "." << fuid.igen << "." << fuid.inum;

    return tapeName.str();
}

std::string Server::getTapeName(unsigned long fsid_h, unsigned long fsid_l,
        unsigned int igen, unsigned long ino, std::string tapeId)

{
    std::stringstream tapeName;

    tapeName << inventory->getMountPoint() << Const::DELIM << tapeId
            << Const::DELIM << Const::LTFSDM_DATA_DIR << Const::DELIM
            << Const::LTFS_NAME << "." << fsid_h << "." << fsid_l << "." << igen
            << "." << ino;

    return tapeName.str();
}

long Server::getStartBlock(std::string tapeName, int fd)

{
    long size;
    char startBlockStr[32];
    long startBlock;

    memset(startBlockStr, 0, sizeof(startBlockStr));

    fsync(fd);

    size = fgetxattr(fd, Const::LTFS_START_BLOCK.c_str(), startBlockStr,
            sizeof(startBlockStr));

    if (size == -1) {
        TRACE(Trace::error, tapeName, errno);
        return Const::UNSET;
    }

    startBlock = strtol(startBlockStr, NULL, 0);

    if (startBlock == LONG_MIN || startBlock == LONG_MAX)
        return Const::UNSET;
    else
        return startBlock;
}

void Server::createDir(std::string tapeId, std::string path)
{
    struct stat statbuf;
    int retry = Const::LTFS_OPERATION_RETRY;

    while (retry > 0) {
        if (Server::statTapeRetry(tapeId, path.c_str(), &statbuf) == -1) {
            if ( errno == ENOENT) {
                if (mkdir(path.c_str(), 0600) == -1) {
                    if ( errno == EBUSY) {
                        sleep(1);
                        retry--;
                        continue;
                    }
                    if ( errno == EEXIST)
                        return;
                    MSG(LTFSDMS0093E, path, errno);
                    THROW(Error::GENERAL_ERROR, errno);
                }
            } else {
                MSG(LTFSDMS0094E, path, errno);
                THROW(Error::GENERAL_ERROR, errno);
            }
        } else if (!S_ISDIR(statbuf.st_mode)) {
            MSG(LTFSDMS0095E, path);
            THROW(Error::GENERAL_ERROR, statbuf.st_mode);
        } else {
            return;
        }
    }
}

void Server::createLink(std::string tapeId, std::string origPath,
        std::string dataPath)
{
    unsigned int pos = 0;
    unsigned int next = 0;
    std::stringstream link;
    std::stringstream dataSubPath;
    std::string relPath = "";
    int retry = Const::LTFS_OPERATION_RETRY;

    link << inventory->getMountPoint() << Const::DELIM << tapeId << origPath;

    dataSubPath << inventory->getMountPoint() << Const::DELIM << tapeId
            << Const::DELIM;
    pos = dataSubPath.str().size() + 1;

    while ((next = link.str().find('/', pos)) < link.str().size()) {
        std::string subs = link.str().substr(0, next);
        createDir(tapeId, subs);
        pos = next + 1;
        relPath.append("../");
    }

    relPath.append(dataPath.substr(dataSubPath.str().size(), dataPath.size()));

    unlink(link.str().c_str());

    while (retry > 0) {
        if (symlink(relPath.c_str(), link.str().c_str()) == -1) {
            if ( errno == EBUSY) {
                sleep(1);
                retry--;
                continue;
            }
            MSG(LTFSDMS0096E, link.str(), errno);
            THROW(Error::GENERAL_ERROR, errno);
        }
        return;
    }
}

void Server::createDataDir(std::string tapeId)
{
    std::stringstream tapeDir;

    tapeDir << inventory->getMountPoint() << Const::DELIM << tapeId
            << Const::DELIM << Const::LTFSDM_DATA_DIR;
    createDir(tapeId, tapeDir.str());
}

void Server::signalHandler(sigset_t set, long key)

{
    int sig;
    int requestNumber = ++globalReqNumber;

    while ( true) {
        if (sigwait(&set, &sig))
            continue;

        if (sig == SIGPIPE) {
            MSG(LTFSDMS0048E);
            continue;
        }

        MSG(LTFSDMS0085I);

        MSG(LTFSDMS0049I, sig);

        LTFSDmCommClient commCommand(Const::CLIENT_SOCKET_FILE);

        try {
            commCommand.connect();
        } catch (const std::exception& e) {
            TRACE(Trace::error, e.what());
            goto end;
        }

        TRACE(Trace::always, requestNumber);
        bool finished = false;

        do {
            LTFSDmProtocol::LTFSDmStopRequest *stopreq =
                    commCommand.mutable_stoprequest();
            stopreq->set_key(key);
            stopreq->set_reqnumber(requestNumber);
            stopreq->set_forced(false);
            stopreq->set_finish(true);

            try {
                commCommand.send();
            } catch (const std::exception& e) {
                TRACE(Trace::error, e.what());
                goto end;
            }

            try {
                commCommand.recv();
            } catch (const std::exception& e) {
                TRACE(Trace::error, e.what());
                goto end;
            }

            const LTFSDmProtocol::LTFSDmStopResp stopresp =
                    commCommand.stopresp();

            finished = stopresp.success();

            if (!finished) {
                sleep(1);
            } else {
                break;
            }
        } while (true);

        if (sig == SIGUSR1)
            goto end;
    }

    end:
    MSG(LTFSDMS0086I);
}

void Server::lockServer()

{
    int lockfd;

    if ((lockfd = open(Const::SERVER_LOCK_FILE.c_str(), O_RDWR | O_CREAT, 0600))
            == -1) {
        MSG(LTFSDMS0001E);
        TRACE(Trace::error, Const::SERVER_LOCK_FILE, errno);
        THROW(Error::GENERAL_ERROR, errno);
    }

    if (flock(lockfd, LOCK_EX | LOCK_NB) == -1) {
        TRACE(Trace::error, errno);
        if ( errno == EWOULDBLOCK) {
            MSG(LTFSDMS0002I);
            THROW(Error::GENERAL_ERROR, errno);
        } else {
            MSG(LTFSDMS0001E);
            TRACE(Trace::error, errno);
            THROW(Error::GENERAL_ERROR, errno);
        }
    }
}

void Server::writeKey()

{
    std::ofstream keyFile;

    keyFile.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try {
        keyFile.open(Const::KEY_FILE, std::fstream::out | std::fstream::trunc);
    } catch (const std::exception& e) {
        TRACE(Trace::error, e.what());
        MSG(LTFSDMS0003E);
        THROW(Error::GENERAL_ERROR);
    }

    srandom(time(NULL));
    key = random();
    keyFile << key << std::endl;

    keyFile.close();
}

void Server::initialize(bool dbUseMemory)

{
    //! [set resource limits]
    if (setrlimit(RLIMIT_NOFILE, &Const::NOFILE_LIMIT) == -1) {
        MSG(LTFSDMS0046E);
        THROW(Error::GENERAL_ERROR, errno);
    }

    if (setrlimit(RLIMIT_NPROC, &Const::NPROC_LIMIT) == -1) {
        MSG(LTFSDMS0046E);
        THROW(Error::GENERAL_ERROR, errno);
    }
    //! [set resource limits]

    lockServer();
    writeKey();

    unlink(Const::CLIENT_SOCKET_FILE.c_str());
    unlink(Const::RECALL_SOCKET_FILE.c_str());

    //! [init db]
    try {
        DB.cleanup();
        DB.open(dbUseMemory);
        DB.createTables();
    } catch (const std::exception& e) {
        TRACE(Trace::error, e.what());
        MSG(LTFSDMS0014E);
        THROW(Error::GENERAL_ERROR);
    }
    //! [init db]
}

void Server::daemonize()

{
    pid_t pid, sid;
    int dev_null;

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        THROW(Error::OK);
    }

    sid = setsid();
    if (sid < 0) {
        MSG(LTFSDMS0012E);
        THROW(Error::GENERAL_ERROR, sid);
    }

    TRACE(Trace::always, getpid());

    messageObject.setLogType(Message::LOGFILE);

    /* redirect stdout to log file */
    if ((dev_null = open("/dev/null", O_RDWR)) == -1) {
        MSG(LTFSDMS0013E);
        THROW(Error::GENERAL_ERROR, errno);
    }
    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);
    close(dev_null);
}

void Server::run(sigset_t set)

{
    SubServer subs;
    Scheduler sched;
    Receiver recv;
    TransRecall trec;
    std::shared_ptr<Connector> connector(nullptr);

    Server::terminate = false;
    Server::forcedTerminate = false;
    Server::finishTerminate = false;

    //! [read the configuration file]
    try {
        Server::conf.read();
    } catch (const std::exception& e) {
        MSG(LTFSDMX0038E);
        goto end;
    }
    //! [read the configuration file]

    try {
        //! [inventorize]
        inventory = new LTFSDMInventory();
        //! [inventorize]
        //! [connector]
        connector = std::shared_ptr<Connector>(
                new Connector(true, &Server::conf));
        //! [connector]
    } catch (const std::exception& e) {
        TRACE(Trace::error, e.what());
        goto end;
    }

    //! [thread pool for stubbing]
    Server::wqs = new ThreadPool<Migration::mig_info_t,
            std::shared_ptr<std::list<unsigned long>>, FsObj::file_state>(
            &Migration::changeFileState, Const::MAX_STUBBING_THREADS,
            "stub1-wq");
    //! [thread pool for stubbing]

    subs.enqueue("Scheduler", &Scheduler::run, &sched, key);
    subs.enqueue("SigHandler", &Server::signalHandler, set, key);
    subs.enqueue("Receiver", &Receiver::run, &recv, key, connector);
    subs.enqueue("RecallD", &TransRecall::run, &trec, connector);

    subs.waitAllRemaining();

    MSG(LTFSDMS0087I);

    TRACE(Trace::always, (bool) Server::terminate,
            (bool) Server::forcedTerminate, (bool) Server::finishTerminate);

    delete (Server::wqs);

    end:

    if (inventory)
        delete (inventory);

    MSG(LTFSDMS0088I);

}
