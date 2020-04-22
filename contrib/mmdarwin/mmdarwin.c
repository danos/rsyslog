/* Copyright 2019 Advens
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
#include "rsyslog.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include "conf.h"
#include "syslogd-types.h"
#include "srUtils.h"
#include "template.h"
#include "module-template.h"
#include "errmsg.h"
#include "parserif.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <uuid/uuid.h>
#include <json.h>

#include "protocol.h" /* custom file written for Darwin */

#define JSON_IPLOOKUP_NAME "!srcip"
#define JSON_LOOKUP_NAME "!mmdarwin"
#define INVLD_SOCK -1
#define INITIAL_BUFFER_SIZE 32
#define BUFFER_DEFAULT_MAX_SIZE 65536

MODULE_TYPE_OUTPUT
MODULE_TYPE_NOKEEP
MODULE_CNFNAME("mmdarwin")

DEFobjCurrIf(glbl)
DEF_OMOD_STATIC_DATA

typedef struct dyn_buffer_t
{
	char *buffer;
	size_t bufferAllocSize;
	size_t bufferMsgSize;
	size_t bufferMaxSize;
} dyn_buffer;

/* config variables */
typedef struct _instanceData
{
	char *pCertitudeKey;				/* the key name to save in the enriched log
							   line the certitude obtained from Darwin */
	uchar *pSockName;				/* the socket path of the filter which will be used by
							   Darwin */
	unsigned long long int filterCode;		/* the filter code associated to the filter which will be used
							   by Darwin */
	enum darwin_filter_response_type response;	/* the type of response for Darwin: no / back / darwin / both */
	struct
	{
		int nmemb;
		char **name;
		char **varname;
	} fieldList; /* our keys (fields) to be extracted from the JSON-parsed log line */
	unsigned int socketMaxUse;
	sbool sendPartial;
} instanceData;

typedef struct wrkrInstanceData
{
	instanceData *pData;
	int sock;				 /* the socket of the filter which will be used by Darwin */
	struct sockaddr_un addr; /* the sockaddr_un used to connect to the Darwin filter */
	uint8_t pktSentSocket;
	dyn_buffer darwinBody; /* the body object used (and reused) to hold data to send to Darwin */
	dyn_buffer fieldBuffer;
} wrkrInstanceData_t;

struct modConfData_s
{
	/* our overall config object */
	rsconf_t *pConf;
	const char *container;
};

/* modConf ptr to use for the current load process */
static modConfData_t *loadModConf = NULL;
/* modConf ptr to use for the current exec process */
static modConfData_t *runModConf = NULL;

/* module-global parameters */
static struct cnfparamdescr modpdescr[] = {
	{"container", eCmdHdlrGetWord, 0},
};
static struct cnfparamblk modpblk =
	{CNFPARAMBLK_VERSION,
	 sizeof(modpdescr) / sizeof(struct cnfparamdescr),
	 modpdescr};

/* tables for interfacing with the v6 config system
 * action (instance) parameters */
static struct cnfparamdescr actpdescr[] = {
	{"key", eCmdHdlrGetWord, CNFPARAM_REQUIRED},
	{"socketpath", eCmdHdlrGetWord, CNFPARAM_REQUIRED},
	{"fields", eCmdHdlrArray, CNFPARAM_REQUIRED},
	{"filtercode", eCmdHdlrGetWord, 0},			/* optional parameter */
	{"response", eCmdHdlrGetWord, 0},			/* optional parameter */
	{"send_partial", eCmdHdlrBinary, 0},		/* optional parameter */
	{"socket_max_use", eCmdHdlrPositiveInt, 0}, /* optional parameter - will disappear in future updates */
};
static struct cnfparamblk actpblk = {
	CNFPARAMBLK_VERSION,
	sizeof(actpdescr) / sizeof(struct cnfparamdescr),
	actpdescr};

/* custom functions */
#define min(a, b) \
	({ __typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a < _b ? _a : _b; })

static rsRetVal openSocket(wrkrInstanceData_t *pWrkrData);
static rsRetVal closeSocket(wrkrInstanceData_t *pWrkrData);
static rsRetVal doTryResume(wrkrInstanceData_t *pWrkrData);

static rsRetVal sendMsg(wrkrInstanceData_t *pWrkrData, void *msg, size_t len);
static rsRetVal receiveMsg(wrkrInstanceData_t *pWrkrData, void *response, size_t len);

int get_field(smsg_t *const pMsg, const char *pFieldName, char **ppRetString);
int expand_buffer(dyn_buffer *pBody, size_t new_size);
int add_field_to_body(dyn_buffer *pBody, const char *field, size_t size);
int start_new_line(dyn_buffer *pBody);
int end_body(dyn_buffer *pBody);

/* open socket to remote system
 */
static rsRetVal openSocket(wrkrInstanceData_t *pWrkrData)
{
	DEFiRet;
	assert(pWrkrData->sock == INVLD_SOCK);

	if ((pWrkrData->sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
	{
		char errStr[1024];
		int eno = errno;
		DBGPRINTF("mmdarwin::openSocket:: error %d creating AF_UNIX/SOCK_STREAM: %s.\n",
				  eno, rs_strerror_r(eno, errStr, sizeof(errStr)));
		pWrkrData->sock = INVLD_SOCK;
		ABORT_FINALIZE(RS_RET_NO_SOCKET);
	}

	memset(&pWrkrData->addr, 0, sizeof(struct sockaddr_un));
	pWrkrData->addr.sun_family = AF_UNIX;
	strncpy(pWrkrData->addr.sun_path, (char *)pWrkrData->pData->pSockName, sizeof(pWrkrData->addr.sun_path) - 1);

	DBGPRINTF("mmdarwin::openSocket:: connecting to Darwin...\n");

	if (connect(pWrkrData->sock, (struct sockaddr *)&pWrkrData->addr, sizeof(struct sockaddr_un)) == -1)
	{
		LogError(errno, RS_RET_NO_SOCKET, "mmdarwin::openSocket:: error connecting to Darwin "
										  "via socket '%s'",
				 pWrkrData->pData->pSockName);

		pWrkrData->sock = INVLD_SOCK;
		ABORT_FINALIZE(RS_RET_NO_SOCKET);
	}

	DBGPRINTF("mmdarwin::openSocket:: connected !\n");
finalize_it:
	if (iRet != RS_RET_OK)
	{
		closeSocket(pWrkrData);
	}
	RETiRet;
}

/* close socket to remote system
 */
static rsRetVal closeSocket(wrkrInstanceData_t *pWrkrData)
{
	DEFiRet;
	if (pWrkrData->sock != INVLD_SOCK)
	{
		if (close(pWrkrData->sock) != 0)
		{
			char errStr[1024];
			int eno = errno;
			DBGPRINTF("mmdarwin::closeSocket:: error %d closing the socket: %s.\n",
					  eno, rs_strerror_r(eno, errStr, sizeof(errStr)));
		}
		pWrkrData->sock = INVLD_SOCK;
	}
	RETiRet;
}

/* try to resume connection if it is not ready
 */
static rsRetVal doTryResume(wrkrInstanceData_t *pWrkrData)
{
	DEFiRet;

	DBGPRINTF("mmdarwin::doTryResume:: trying to resume\n");
	closeSocket(pWrkrData);
	iRet = openSocket(pWrkrData);

	if (iRet != RS_RET_OK)
	{
		iRet = RS_RET_SUSPENDED;
	}

	RETiRet;
}

/* send a message via TCP
 * inspired by rgehards, 2007-12-20
 */
static rsRetVal sendMsg(wrkrInstanceData_t *pWrkrData, void *msg, size_t len)
{
	DEFiRet;

	DBGPRINTF("mmdarwin::sendMsg:: sending message to Darwin...\n");

	if (pWrkrData->sock == INVLD_SOCK)
	{
		CHKiRet(doTryResume(pWrkrData));
	}

	if (pWrkrData->sock != INVLD_SOCK)
	{
		if (send(pWrkrData->sock, msg, len, 0) == -1)
		{
			char errStr[1024];
			DBGPRINTF("mmdarwin::sendData:: error while sending data: error[%d] -> %s\n",
					  errno, rs_strerror_r(errno, errStr, sizeof(errStr)));
			iRet = RS_RET_SUSPENDED;
		}
	}

finalize_it:
	RETiRet;
}

/* receive a message via TCP
 * inspired by rgehards, 2007-12-20
 */
static rsRetVal receiveMsg(wrkrInstanceData_t *pWrkrData, void *response, size_t len)
{
	DEFiRet;

	DBGPRINTF("mmdarwin::receiveMsg:: receiving message from Darwin...\n");

	if (pWrkrData->sock == INVLD_SOCK)
	{
		CHKiRet(doTryResume(pWrkrData));
	}

	if (pWrkrData->sock != INVLD_SOCK)
	{
		if (recv(pWrkrData->sock, response, len, MSG_WAITALL) <= 0)
		{
			char errStr[1024];
			DBGPRINTF("mmdarwin::receiveMsg:: error while receiving data: error[%d] -> %s\n",
					  errno, rs_strerror_r(errno, errStr, sizeof(errStr)));
			iRet = RS_RET_NONE;
		}
	}

finalize_it:
	RETiRet;
}

/**
 * Get the string corresponding to a field supposedly present in the provided message
 *
 * params:
 *  - pMsg: a pointer to the rsyslog message where the field should be
 *  - pFieldName: a nul-terminated pointer to string representing the name of the field to search for
 *  - ppRetString: the pointer to contain the potential return string
 *
 * return: 1 if a string was put in ppRetString, 0 otherwise
 *
 * note: the string placed in ppRetString should be freed by the caller
 */
int get_field(smsg_t *const pMsg, const char *pFieldName, char **ppRetString)
{
	DBGPRINTF("mmdarwin::get_field:: getting key '%s' in msg\n", pFieldName);
	struct json_object *pJson = NULL;
	char *pFieldString = NULL;
	int retVal = 0;

	msgPropDescr_t propDesc;
	msgPropDescrFill(&propDesc, (uchar *)pFieldName, strlen(pFieldName));
	msgGetJSONPropJSONorString(pMsg, &propDesc, &pJson, (uchar **)&pFieldString);

	if (pFieldString)
	{
		*ppRetString = pFieldString;
		DBGPRINTF("mmdarwin::get_field:: got string\n");
		retVal = 1;
	}
	else if (pJson)
	{
		pFieldString = (char *)json_object_get_string(pJson);
		if (pFieldString)
		{
			*ppRetString = strdup(pFieldString);
			retVal = 1;
			DBGPRINTF("mmdarwin::get_field:: got string from json\n");
			json_object_put(pJson);
		}
	}

	msgPropDescrDestruct(&propDesc);
	return retVal;
}

/**
 * expands the buffer object in the dyn_buffer object
 *
 * params:
 *  - pBody: a pointer to the concerned structure to expand
 *  - new_size: the new size to give to the underlying buffer
 *
 * return: 0 if the expansion was successful, -1 otherwise
 */
int expand_buffer(dyn_buffer *pBody, size_t new_size)
{
	/* return error if new_size tries to exceed max defined size */
	if (new_size > pBody->bufferMaxSize)
		return -1;
	while (pBody->bufferAllocSize < new_size)
		pBody->bufferAllocSize += INITIAL_BUFFER_SIZE;

	DBGPRINTF("mmdarwin::expand_buffer:: expanding buffer to %zu\n", pBody->bufferAllocSize);

	char *tmp = realloc(pBody->buffer, pBody->bufferAllocSize * sizeof(char));

	if (!tmp)
	{
		DBGPRINTF("mmdarwin::expand_buffer:: could not resize buffer\n");
		return -1;
	}

	pBody->buffer = tmp;
	return 0;
}

/**
 * adds a field to the dyn_buffer buffer
 *
 * params:
 *  - pBody: the pointer on the dyn_buffer structure
 *  - field: the potentially not null-terminated string to add as a field to the dyn_buffer
 *  - size: the size of the string (without the '\0' character)
 *
 * return: 0 if the field was indeed added to the dyn_buffer, -1 otherwise
 */
int add_field_to_body(dyn_buffer *pBody, const char *field, size_t size)
{
	/* get required additional size for field, quotes, colon, and \0
	and potentially also for the beginning of the message structure */
	int beginning = (pBody->bufferMsgSize == 0) ? 2 : 0;
	size_t requiredBodySize = pBody->bufferMsgSize + size + 4 + beginning;

	/* resize body buffer if necessary */
	if (requiredBodySize > pBody->bufferAllocSize)
	{
		if (expand_buffer(pBody, requiredBodySize) != 0)
		{
			return -1;
		}
	}

	/* add message structure beginning if current message is empty */
	if (!pBody->bufferMsgSize)
	{
		pBody->buffer[0] = '[';
		pBody->buffer[1] = '[';
		pBody->bufferMsgSize += 2;
	}

	/* add field with quotes and colon */
	pBody->buffer[pBody->bufferMsgSize++] = '\"';
	memcpy((void *)&pBody->buffer[pBody->bufferMsgSize], (const void *)field, size);
	pBody->bufferMsgSize += size;
	pBody->buffer[pBody->bufferMsgSize++] = '\"';
	pBody->buffer[pBody->bufferMsgSize++] = ',';

	return 0;
}

/**
 * small helper function to start a new input line (used for bulk-calls) in the dyn_buffer.
 * will close current line with a ']' and start the next with a '['.
 * will also remove leading ',' in fields list.
 *
 * params:
 *  - pBody: the pointer on the dyn_buffer on which to start a new input line
 *
 * return: 0 if successful, -1 otherwise
 */
int start_new_line(dyn_buffer *pBody)
{
	/* don't if the message is empty */
	if (!pBody->bufferMsgSize)
	{
		return -1;
	}

	DBGPRINTF("mmdarwin::start_new_line:: starting new line entry in body\n");

	if (pBody->bufferAllocSize < pBody->bufferMsgSize + 2)
	{
		if (expand_buffer(pBody, pBody->bufferAllocSize + 2) != 0)
		{
			return -1;
		}
	}

	pBody->buffer[pBody->bufferMsgSize - 1] = ']';
	pBody->buffer[pBody->bufferMsgSize++] = ',';
	pBody->buffer[pBody->bufferMsgSize++] = '[';
	return 0;
}

/**
 * small helper function to close the dyn_buffer structure.
 * will close the line list with two ']' and will remove the leading ',' in the fields list
 *
 * params:
 *  - pBody: the pointer on the dyn_buffer on which to start a new input line
 *
 * return: 0 if successful, -1 otherwise
 */
int end_body(dyn_buffer *pBody)
{
	/* don't if the message is empty */
	if (!pBody->bufferMsgSize)
	{
		return -1;
	}

	DBGPRINTF("mmdarwin::end_body:: finishing body structure\n");

	if (pBody->bufferAllocSize < pBody->bufferMsgSize + 2)
	{
		if (expand_buffer(pBody, pBody->bufferAllocSize + 2) != 0)
		{
			return -1;
		}
	}

	pBody->buffer[pBody->bufferMsgSize - 1] = ']';
	pBody->buffer[pBody->bufferMsgSize++] = ']';
	pBody->buffer[pBody->bufferMsgSize++] = '\0';
	return 0;
}

BEGINbeginCnfLoad
CODESTARTbeginCnfLoad
	loadModConf = pModConf;
pModConf->pConf = pConf;
ENDbeginCnfLoad

BEGINendCnfLoad
CODESTARTendCnfLoad
ENDendCnfLoad

BEGINcheckCnf
CODESTARTcheckCnf
ENDcheckCnf

BEGINactivateCnf
CODESTARTactivateCnf
	runModConf = pModConf;
ENDactivateCnf

BEGINfreeCnf
CODESTARTfreeCnf
	free((void *)runModConf->container);
ENDfreeCnf

BEGINdbgPrintInstInfo
CODESTARTdbgPrintInstInfo
	DBGPRINTF("%s\n", pData->pSockName);
ENDdbgPrintInstInfo

BEGINcreateInstance
CODESTARTcreateInstance
ENDcreateInstance

BEGINcreateWrkrInstance
CODESTARTcreateWrkrInstance
	pWrkrData->pktSentSocket = 0;
	pWrkrData->darwinBody.bufferAllocSize = 0;
	pWrkrData->darwinBody.bufferMaxSize = BUFFER_DEFAULT_MAX_SIZE;
	pWrkrData->darwinBody.bufferMsgSize = 0;
	pWrkrData->sock = INVLD_SOCK;
ENDcreateWrkrInstance

BEGINisCompatibleWithFeature
CODESTARTisCompatibleWithFeature
ENDisCompatibleWithFeature

BEGINfreeInstance
CODESTARTfreeInstance
	if (pData->fieldList.name != NULL)
	{
		for (int i = 0; i < pData->fieldList.nmemb; ++i)
		{
			free(pData->fieldList.name[i]);
			free(pData->fieldList.varname[i]);
		}
		free(pData->fieldList.name);
		free(pData->fieldList.varname);
	}
	free(pData->pCertitudeKey);
	free(pData->pSockName);
ENDfreeInstance

BEGINfreeWrkrInstance
CODESTARTfreeWrkrInstance
	closeSocket(pWrkrData);
	free(pWrkrData->darwinBody.buffer);
ENDfreeWrkrInstance

BEGINsetModCnf
struct cnfparamvals *pvals = NULL;
int i;
CODESTARTsetModCnf
	loadModConf->container = NULL;
	pvals = nvlstGetParams(lst, &modpblk, NULL);
	if (pvals == NULL)
	{
		LogError(0, RS_RET_MISSING_CNFPARAMS,
				"mmdarwin: error processing module config parameters missing [module(...)]");
		ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
	}
	if (Debug)
	{
		DBGPRINTF("mmdarwin::setModCnf:: module (global) param blk for mmdarwin:\n");
		cnfparamsPrint(&modpblk, pvals);
	}

	for (i = 0; i < modpblk.nParams; ++i)
	{
		if (!pvals[i].bUsed)
			continue;
		if (!strcmp(modpblk.descr[i].name, "container"))
		{
			loadModConf->container = es_str2cstr(pvals[i].val.d.estr, NULL);
		}
		else
		{
			DBGPRINTF("mmdarwin::setModCnf:: program error, non-handled "
					"param '%s'\n",
					modpblk.descr[i].name);
		}
	}

	if (loadModConf->container == NULL)
	{
		CHKmalloc(loadModConf->container = strdup(JSON_IPLOOKUP_NAME));
	}

finalize_it :
	if (pvals != NULL)
		cnfparamvalsDestruct(pvals, &modpblk);
ENDsetModCnf

static inline void setInstParamDefaults(instanceData *pData)
{
	DBGPRINTF("mmdarwin::setInstParamDefaults::\n");
	pData->pCertitudeKey = NULL;
	pData->pSockName = NULL;
	pData->fieldList.nmemb = 0;
	pData->filterCode = DARWIN_FILTER_CODE_NO;
	pData->response = DARWIN_RESPONSE_SEND_NO;
	pData->socketMaxUse = 0;
	pData->sendPartial = 0;
}

BEGINnewActInst
	struct cnfparamvals *pvals;
	int i;
CODESTARTnewActInst
	DBGPRINTF("mmdarwin::newActInst::\n");
	if ((pvals = nvlstGetParams(lst, &actpblk, NULL)) == NULL)
	{
		ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
	}

	CODE_STD_STRING_REQUESTnewActInst(1)
	CHKiRet(OMSRsetEntry(*ppOMSR, 0, NULL, OMSR_TPL_AS_MSG));
	CHKiRet(createInstance(&pData));
	setInstParamDefaults(pData);

	for (i = 0; i < actpblk.nParams; ++i)
	{
		if (!pvals[i].bUsed)
			continue;

		if (!strcmp(actpblk.descr[i].name, "key"))
		{
			pData->pCertitudeKey = es_str2cstr(pvals[i].val.d.estr, NULL);
			DBGPRINTF("mmdarwin::newActInst:: certitudeKey is %s\n", pData->pCertitudeKey);
		}
		else if (!strcmp(actpblk.descr[i].name, "socketpath"))
		{
			pData->pSockName = (uchar *)es_str2cstr(pvals[i].val.d.estr, NULL);
			DBGPRINTF("mmdarwin::newActInst:: sockName is %s\n", pData->pSockName);
		}
		else if (!strcmp(actpblk.descr[i].name, "socket_max_use"))
		{
			pData->socketMaxUse = (uint32_t)pvals[i].val.d.n;
			DBGPRINTF("mmdarwin::newActInst:: socketMaxUse is %d\n", pData->socketMaxUse);
		}
		else if (!strcmp(actpblk.descr[i].name, "send_partial"))
		{
			pData->sendPartial = (sbool)pvals[i].val.d.n;
			if (pData->sendPartial)
			{
				DBGPRINTF("mmdarwin::newActInst:: sending bodies even if fields are missing\n");
			}
			else
			{
				DBGPRINTF("mmdarwin::newActInst:: only sending complete bodies\n");
			}
		}
		else if (!strcmp(actpblk.descr[i].name, "response"))
		{
			char *response = es_str2cstr(pvals[i].val.d.estr, NULL);

			if (!strcmp(response, "no"))
			{
				pData->response = DARWIN_RESPONSE_SEND_NO;
				DBGPRINTF("mmdarwin::newActInst:: response type is 'no'\n");
			}
			else if (!strcmp(response, "back"))
			{
				pData->response = DARWIN_RESPONSE_SEND_BACK;
				DBGPRINTF("mmdarwin::newActInst:: response type is 'back'\n");
			}
			else if (!strcmp(response, "darwin"))
			{
				pData->response = DARWIN_RESPONSE_SEND_DARWIN;
				DBGPRINTF("mmdarwin::newActInst:: response type is 'darwin'\n");
			}
			else if (!strcmp(response, "both"))
			{
				pData->response = DARWIN_RESPONSE_SEND_BOTH;
				DBGPRINTF("mmdarwin::newActInst:: response type is 'both'\n");
			}
			else
			{
				DBGPRINTF(
					"mmdarwin::newActInst:: invalid 'response' value: %s. 'No response' set.\n",
					response);

				pData->response = DARWIN_RESPONSE_SEND_NO;
				DBGPRINTF("mmdarwin::newActInst:: response type is 'no'\n");
			}

			free(response);
		}
		else if (!strcmp(actpblk.descr[i].name, "filtercode"))
		{
			char *filterCode = es_str2cstr(pvals[i].val.d.estr, NULL);
			pData->filterCode = strtoull(filterCode, NULL, 16);
			free(filterCode);
		}
		else if (!strcmp(actpblk.descr[i].name, "fields"))
		{
			pData->fieldList.nmemb = pvals[i].val.d.ar->nmemb;
			CHKmalloc(pData->fieldList.name = calloc(pData->fieldList.nmemb, sizeof(char *)));
			CHKmalloc(pData->fieldList.varname = calloc(pData->fieldList.nmemb, sizeof(char *)));

			for (int j = 0; j < pvals[i].val.d.ar->nmemb; ++j)
			{
				char *const param = es_str2cstr(pvals[i].val.d.ar->arr[j], NULL);
				char *varname = NULL;
				char *name;
				if (*param == ':')
				{
					char *b = strchr(param + 1, ':');
					if (b == NULL)
					{
						parser_errmsg(
							"mmdarwin::newActInst:: missing closing colon: '%s'", param);
						ABORT_FINALIZE(RS_RET_ERR);
					}

					*b = '\0'; /* split name & varname */
					varname = param + 1;
					name = b + 1;
				}
				else
				{
					name = param;
				}
				CHKmalloc(pData->fieldList.name[j] = strdup(name));
				char vnamebuf[1024];
				snprintf(vnamebuf, sizeof(vnamebuf),
						"%s!%s", loadModConf->container,
						(varname == NULL) ? name : varname);
				CHKmalloc(pData->fieldList.varname[j] = strdup(vnamebuf));
				free(param);
				DBGPRINTF("mmdarwin::newActInst:: will look for field %s\n", pData->fieldList.name[j]);
			}
		}
		else
		{
			DBGPRINTF(
			"mmdarwin::newActInst:: program error, non-handled param '%s'\n", actpblk.descr[i].name);
		}
	}
CODE_STD_FINALIZERnewActInst
	cnfparamvalsDestruct(pvals, &actpblk);
ENDnewActInst

BEGINtryResume
CODESTARTtryResume
	iRet = doTryResume(pWrkrData);
ENDtryResume

BEGINdoAction_NoStrings
	smsg_t **ppMsg = (smsg_t **)pMsgData; /* the raw data */
	smsg_t *pMsg = ppMsg[0]; /* the raw log line */
	instanceData *pData = pWrkrData->pData; /* the parameters given for the plugin */
	char *pFieldValue = NULL; /* ponter to the found field value */
	int fieldsNum = 0; /* number of fields retrieved */
	struct json_object *pJson = json_object_new_object(); /* json structure to add info to Rsyslog message */

CODESTARTdoAction
	DBGPRINTF("mmdarwin::doAction:: processing log line: '%s'\n", getMSG(pMsg));
	pWrkrData->darwinBody.bufferMsgSize = 0;
	fieldsNum = 0;

	for (int i = 0; i < pData->fieldList.nmemb; i++)
	{
		DBGPRINTF("mmdarwin::doAction:: processing field '%s'\n", pData->fieldList.name[i]);
		pFieldValue = NULL;

		/* case 1: static field. We simply forward it to Darwin */
		if (pData->fieldList.name[i][0] != '!')
		{
			pFieldValue = strdup(pData->fieldList.name[i]);
			/* case 2: dynamic field. We retrieve its value from the JSON logline and forward it to
			 * Darwin */
		}
		else
		{
			if (!get_field(pMsg, pData->fieldList.name[i], &pFieldValue))
			{
				DBGPRINTF("mmdarwin::doAction:: \
could not extract field '%s' from message\n", pData->fieldList.name[i]);
				continue;
			}
		}

		DBGPRINTF(
			"mmdarwin::doAction:: got value of field '%s': '%s'\n", pData->fieldList.name[i], pFieldValue);

		if (add_field_to_body(&(pWrkrData->darwinBody), pFieldValue, strlen(pFieldValue)) != 0)
		{
			DBGPRINTF("mmdarwin::doAction:: could not add field to body, aborting\n");
			free(pFieldValue);
			ABORT_FINALIZE(RS_RET_ERR);
		}

		fieldsNum++;
		free(pFieldValue);
	}

	if (fieldsNum)
	{
		if (!pData->sendPartial && fieldsNum != pData->fieldList.nmemb)
		{
			DBGPRINTF("mmdarwin::doAction:: not all fields could be retrieved, not sending partial message.\
	(if you wish to send partial messages anyway, set 'send_partial' to 'on' in instance parameters)\n");
			FINALIZE;
		}
		if (end_body(&(pWrkrData->darwinBody)) != 0)
			ABORT_FINALIZE(RS_RET_ERR);
	}
	else
	{
		DBGPRINTF("mmdarwin::doAction:: no fields retrieved, finalizing\n");
		FINALIZE;
	}

	DBGPRINTF("mmdarwin::doAction:: body to send: '%s'\n", pWrkrData->darwinBody.buffer);

	if (pData->socketMaxUse)
	{
		/* need to rotate socket connections */
		if (!pWrkrData->pktSentSocket)
		{
			DBGPRINTF("mmdarwin::doAction:: opening a new connection\n");
			CHKiRet(doTryResume(pWrkrData));
		}
		pWrkrData->pktSentSocket = (pWrkrData->pktSentSocket + 1) % pData->socketMaxUse;
	}

	/* the Darwin header to be sent to the filter */
	darwin_filter_packet_t header = {
		.type = DARWIN_PACKET_OTHER,
		.response = pData->response,
		.filter_code = pData->filterCode,
		.body_size = pWrkrData->darwinBody.bufferMsgSize};
	/* generate uuid for Darwin event id */
	uuid_generate(header.evt_id);

	DBGPRINTF("mmdarwin::doAction:: sending header to Darwin\n");
	CHKiRet(sendMsg(pWrkrData, &header, sizeof(darwin_filter_packet_t)));

	DBGPRINTF("mmdarwin::doAction:: sending body to Darwin\n");
	CHKiRet(sendMsg(pWrkrData, (void *)(pWrkrData->darwinBody.buffer), pWrkrData->darwinBody.bufferMsgSize));

	/* there is no need to wait for a response that will never come */
	if (pData->response == DARWIN_RESPONSE_SEND_NO || pData->response == DARWIN_RESPONSE_SEND_DARWIN)
	{
		DBGPRINTF("mmdarwin::doAction:: no response will be sent back "
				"(darwin response type is set to 'no' or 'darwin')\n");
		char uuidStr[40];
		uuid_unparse(header.evt_id, uuidStr);
		DBGPRINTF("uuid: %s\n", uuidStr);
		json_object_object_add(pJson, "darwin_id", json_object_new_string(uuidStr));
		goto finalize_it;
	}

	darwin_filter_packet_t response;
	memset(&response, 0, sizeof(response));
	DBGPRINTF("mmdarwin::doAction:: receiving from Darwin\n");
	CHKiRet(receiveMsg(pWrkrData, &response, sizeof(response)));

	unsigned int certitude = response.certitude_list[0];
	DBGPRINTF("mmdarwin::doAction:: end of the transaction, certitude is %d\n", certitude);

	json_object_object_add(pJson, pData->pCertitudeKey, json_object_new_int(certitude));

finalize_it :
	if (json_object_get_member_count(pJson))
	{
		DBGPRINTF("mmdarwin::doAction:: adding entry to rsyslog message\n");
		msgAddJSON(pMsg, (uchar *)JSON_LOOKUP_NAME, pJson, 0, 0);
	}
	else {
		json_object_put(pJson);
	}
	DBGPRINTF("mmdarwin::doAction:: finished processing log line\n");

ENDdoAction

NO_LEGACY_CONF_parseSelectorAct

BEGINmodExit
CODESTARTmodExit
	objRelease(glbl, CORE_COMPONENT);
ENDmodExit

BEGINqueryEtryPt
CODESTARTqueryEtryPt
CODEqueryEtryPt_STD_OMOD_QUERIES
CODEqueryEtryPt_STD_OMOD8_QUERIES
CODEqueryEtryPt_STD_CONF2_setModCnf_QUERIES
CODEqueryEtryPt_STD_CONF2_OMOD_QUERIES
CODEqueryEtryPt_STD_CONF2_QUERIES
ENDqueryEtryPt

BEGINmodInit()
CODESTARTmodInit
	/* we only support the current interface specification */
	*ipIFVersProvided = CURR_MOD_IF_VERSION;
CODEmodInit_QueryRegCFSLineHdlr
	DBGPRINTF("mmdarwin::modInit:: module compiled with rsyslog version %s.\n", VERSION);
	CHKiRet(objUse(glbl, CORE_COMPONENT));
ENDmodInit
