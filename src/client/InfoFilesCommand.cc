#include <unistd.h>
#include <string>

#include "src/common/messages/Message.h"
#include "src/common/tracing/Trace.h"
#include "src/common/errors/errors.h"

#include "OpenLTFSCommand.h"
#include "InfoFilesCommand.h"

void InfoFilesCommand::printUsage()
{
	INFO(LTFSDMC0010I);
}

void InfoFilesCommand::doCommand(int argc, char **argv)
{
	if ( argc == 1 ) {
		INFO(LTFSDMC0018E);
		goto error;
	}

	processOptions(argc, argv);

	if ( fileList.compare("") && directoryName.compare("") ) {
		INFO(LTFSDMC0015E);
		goto error;
	}

	if (optind != argc) {
		if (fileList.compare("")) {
			INFO(LTFSDMC0016E);
			goto error;
		}
		if (directoryName.compare("")) {
			INFO(LTFSDMC0017E);
			goto error;
		}
	}
	else if ( !fileList.compare("") && !directoryName.compare("") ) {
		// a least a file or directory needs to be specified
		INFO(LTFSDMC0019E);
		goto error;
	}

	TRACE(Trace::little, fileList);
	TRACE(Trace::little, directoryName);

	return;

error:
	printUsage();
	throw(LTFSDMErr::LTFSDM_GENERAL_ERROR);
}
