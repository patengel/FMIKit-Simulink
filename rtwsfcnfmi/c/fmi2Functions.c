/*****************************************************************
 *  Copyright (c) Dassault Systemes. All rights reserved.        *
 *  This file is part of FMIKit. See LICENSE.txt in the project  *
 *  root for license information.                                *
 *****************************************************************/

/*
-----------------------------------------------------------
	Implementation of FMI 2.0 on top of C code
	generated by Simulink Coder S-function target.
-----------------------------------------------------------
*/

#include "sfcn_fmi.h"
#include "fmi.h"
#include "fmi2Functions.h"	/* Official FMI 2.0 header */
#include "sfunction.h"

typedef struct {
	fmi2CallbackFunctions functions;
	fmi2EventInfo eventInfo;
} UserData;


/* -------------- Macro to check if initialized -------------- */

#define CHECK_INITIALIZED(model, label)                                        \
	if (model->status <= modelInitializationMode) {                             \
        logger(model, model->instanceName, fmi2Warning, "",                        \
			label": model is not initialized\n");                                \
		return fmi2Warning;                                                        \
	}

/* ------------- Macro for unsupported function -------------- */

#define FMI_UNSUPPORTED(label)						\
	Model* model = (Model*) c;					\
	logger(model, model->instanceName, fmi2Warning, "", "fmi2"#label": Currently not supported for FMI S-function\n");\
	return fmi2Warning


/* ------------------ Local help functions ------------------- */

static void logMessage(Model *model, int status, const char *message, ...) {
	UserData *userData = (UserData *)model->userData;
	userData->functions.logger(userData->functions.componentEnvironment, model->instanceName, status, "", message);
}

/* logger wrapper for handling of enabled/disabled logging */
static void logger(fmi2Component c, fmi2String instanceName, fmi2Status status,
				   fmi2String category, fmi2String message, ...);
static fmi2String strDup(const fmi2CallbackFunctions *functions, fmi2String s);

/* ------------------ ODE solver functions ------------------- */
const char *RT_MEMORY_ALLOCATION_ERROR = "Error when allocating SimStruct solver data.";

/* Globals used for child S-functions */
extern Model* currentModel;

/* -----------------------------------------------------------
   ----------------- FMI function definitions ----------------
   ----------------------------------------------------------- */

/***************** Common functions *****************/

const char* fmi2GetTypesPlatform() {
	return fmi2TypesPlatform;
}

const char* fmi2GetVersion() {
	return fmi2Version;
}

fmi2Status fmi2SetDebugLogging(fmi2Component c, fmi2Boolean loggingOn, size_t nCategories, const fmi2String categories[]) {
	Model* model = (Model*) c;
	model->loggingOn = loggingOn;
	/* Categories currently not supported */
	return fmi2OK;
}

void fmi2FreeInstance(fmi2Component c);

fmi2Component fmi2Instantiate(fmi2String	instanceName,
								fmi2Type	fmuType,
								fmi2String	GUID,
								fmi2String	fmuResourceLocation,
								const fmi2CallbackFunctions* functions,
								fmi2Boolean	visible,
								fmi2Boolean	loggingOn)
{
	/* The following arguments are ignored: fmuResourceLocation, visible */

	// TODO: check GUID
	///* verify GUID */
	//if (strcmp(GUID, MODEL_GUID) != 0) {
	//	logMessage(model, fmi2Error, "Invalid GUID: %s, expected %s\n", GUID, MODEL_GUID);
	//	functions->freeMemory(model);
	//	return NULL;
	//}

	// TODO: check logger callback

	UserData *userData = (UserData *)calloc(1, sizeof(UserData));
	userData->functions = *functions;

	Model *model = InstantiateModel(instanceName, logMessage, userData);

	model->isCoSim = fmi2False;
	model->hasEnteredContMode = fmi2False;
	if (fmuType == fmi2CoSimulation) {
		model->isCoSim = fmi2True;
		if (functions->stepFinished != NULL) {
			logMessage(model, fmi2Warning, "fmi2Instantiate: Callback function stepFinished != NULL but asynchronous fmi2DoStep is not supported");
		}
	}

	return model;
}

void fmi2FreeInstance(fmi2Component c)
{
	Model* model = (Model*) c;
    FreeModel(model);
}

fmi2Status fmi2SetTime(fmi2Component c, fmi2Real time);
fmi2Status fmi2SetupExperiment(fmi2Component c,
										fmi2Boolean toleranceDefined,
										fmi2Real tolerance,
										fmi2Real startTime,
										fmi2Boolean stopTimeDefined,
										fmi2Real stopTime)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;

	/* Any supplied tolerance if toleranceDefined is not used */
	if (stopTimeDefined == fmi2True) {
		/* Provide information that stopTime will not be enforced by FMU */
		logger(model, model->instanceName, status, "", "fmi2SetupExperiment: The defined stopTime will not be enforced by the FMU\n");
	}
	if (fabs(startTime) > SFCN_FMI_EPS) {
		status = fmi2Error;
		logger(model, model->instanceName, status, "", "fmi2SetupExperiment: startTime other than 0.0 not supported\n");
	}
	return status;
}

fmi2Status fmi2NewDiscreteStates_(fmi2Component c, fmi2EventInfo* eventInfo);
fmi2Status fmi2EnterInitializationMode(fmi2Component c)
{
	Model* model = (Model*) c;

	/* Initialize continuous-time states */
	if (ssGetmdlInitializeConditions(model->S) != NULL) {
		sfcnInitializeConditions(model->S);
	}
	if (_ssGetRTWGeneratedEnable(model->S) != NULL) {
		_sfcnRTWGeneratedEnable(model->S);
	}
	/* Check Simstruct error status and stop requested */
	if ((ssGetErrorStatus(model->S) != NULL) || (ssGetStopRequested(model->S) != 0)) {
		if (ssGetStopRequested(model->S) != 0) {
			logger(model, model->instanceName, fmi2Error, "", "Stop requested by S-function!\n");
		}
		if (ssGetErrorStatus(model->S) != NULL) {
			logger(model, model->instanceName, fmi2Error, "", "Error reported by S-function: %s\n", ssGetErrorStatus(model->S));
		}
		model->status = modelInitializationMode;
		return fmi2Error;
	}

	model->S->mdlInfo->t[0] = 0.0;
	if (model->fixed_in_minor_step_offset_tid != -1) {
		model->S->mdlInfo->t[model->fixed_in_minor_step_offset_tid] = 0.0;
	}

	if (!(model->isCoSim)) {
		/* Call mdlOutputs for continuous parts */
		if (model->fixed_in_minor_step_offset_tid != -1) {
			model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 1;
			if (SFCN_FMI_LOAD_MEX) {
				copyPerTaskSampleHits(model->S);
			}
		}
		sfcnOutputs(model->S,0);
		_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);
		if (model->fixed_in_minor_step_offset_tid != -1) {
			model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 0;
			if (SFCN_FMI_LOAD_MEX) {
				copyPerTaskSampleHits(model->S);
			}
		}
	}

	model->status = modelInitializationMode;

	logger(model, model->instanceName, fmi2OK, "", "Enter initialization mode\n");
	return fmi2OK;
}

fmi2Status fmi2ExitInitializationMode(fmi2Component c)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;

	model->status = modelEventMode;

	if (model->isCoSim) {
		/* Evaluate model at t=0 */
		status = fmi2NewDiscreteStates(c, &(((UserData *)model->userData)->eventInfo));
		if (status != fmi2OK) {
			model->status = modelInstantiated;
			return status;
		}
	}

	model->shouldRecompute = fmi2True;
	model->lastGetTime = -1.0;  /* to make sure that derivatives are computed in fmi2GetReal at t=0 */

	logger(model, model->instanceName, fmi2OK, "", "Exit initialization mode, enter event mode\n");
	return fmi2OK;
}

fmi2Status fmi2Terminate(fmi2Component c)
{
	Model* model = (Model*) c;

	if (model == NULL) {
		return fmi2OK;
	}

	logger(model, model->instanceName, fmi2OK, "", "Terminating\n");
	model->status = modelTerminated;

	return fmi2OK;
}

fmi2Status fmi2Reset(fmi2Component c)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;
	void* paramP;

	resetSimStructVectors(model->S);
	rt_DestroyIntegrationData(model->S);
	rt_CreateIntegrationData(model->S);
	setSampleStartValues(model);
	if (ssGetUserData(model->S) != NULL ) {
		if (SFCN_FMI_NBR_PARAMS > 0) {
			paramP = sfcn_fmi_getParametersP_(model->S);
			free(paramP);
		}
	}
	sfcnTerminate(model->S);
	if (ssGetmdlStart(model->S) != NULL) {
		sfcnStart(model->S);
	}
	if (ssGetmdlInitializeConditions(model->S) != NULL) {
		sfcnInitializeConditions(model->S);
	}
	sfcn_fmi_assignParameters_(model->S, model->parameters);
	memset(model->oldZC,			0, (SFCN_FMI_ZC_LENGTH+1)*sizeof(real_T));
	memset(model->numSampleHits,	0, (model->S->sizes.numSampleTimes+1)*sizeof(int_T));
	model->fixed_in_minor_step_offset_tid = 0;
	model->nextHit_tid0 = 0.0;
	model->lastGetTime = -1.0;
	model->shouldRecompute = fmi2False;
	model->time = 0.0;
	model->nbrSolverSteps = 0.0;
	model->status = modelInstantiated;

	UserData *userData = (UserData *)model->userData;
	memset(&(userData->eventInfo), 0, sizeof(fmi2EventInfo));

	return status;
}

/***************** Get / Set functions/macros *****************/

static void setValueReal(const void* vPtr, int dataType, fmi2Real value)
{
	switch (dataType) {
		case SS_SINGLE:   /* real32_T  */
			*((real32_T*)vPtr) = (real32_T) value;
			break;
		default:   /* All other cases treated as real_T */
			*((real_T*)vPtr) = (real_T) value;
			break;
	}
}

static void setValueInteger(const void* vPtr, int dataType, fmi2Integer value)
{
	switch (dataType) {
		case SS_INT8:   /* int8_T  */
			*((int8_T*)vPtr) = (int8_T) value;
			break;
		case SS_UINT8:   /* uint8_T  */
			*((uint8_T*)vPtr) = (uint8_T) value;
			break;
		case SS_INT16:   /* int16_T  */
			*((int16_T*)vPtr) = (int16_T) value;
			break;
		case SS_UINT16:   /* uint16_T  */
			*((uint16_T*)vPtr) = (uint16_T) value;
			break;
		case SS_INT32:   /* int32_T  */
			*((int32_T*)vPtr) = (int32_T) value;
			break;
		case SS_UINT32:   /* uint32_T  */
			*((uint32_T*)vPtr) = (uint32_T) value;
			break;
		default:   /* All other cases treated as int32_T */
			*((int32_T*)vPtr) = (int32_T) value;
			break;
	}
}

static void setValueBoolean(const void* vPtr, int dataType, fmi2Boolean value)
{
	/* Should only be called if dataType is indeed boolean_T */
	*((boolean_T*)vPtr) = (boolean_T) value;
}

#define FMI_SET(label)												\
	Model* model = (Model*) c;												\
	size_t i;																\
    fmi2Boolean allowed = fmi2True;											\
	fmi2Boolean paramChanged = fmi2False;											\
																			\
	for (i = 0; i < nvr; i++) {												\
		const fmi2ValueReference r = vr[i];									\
		int index    = SFCN_FMI_INDEX(r);									\
		int dataType = SFCN_FMI_DATATYPE(r);								\
																			\
		switch (SFCN_FMI_CATEGORY(r)) {										\
        case SFCN_FMI_INPUT:												\
			setValue ## label(model->inputs[index], dataType, value[i]);	\
			model->shouldRecompute = fmi2True;\
			break;															\
		case SFCN_FMI_STATE:												\
			if (model->isCoSim && model->status != modelInstantiated) {\
				allowed = fmi2False;	\
			} else {\
				ssGetContStates(model->S)[index] = (real_T)value[i];			\
				model->shouldRecompute = fmi2True;\
			}\
			break;															\
		case SFCN_FMI_DERIVATIVE:\
			if (model->status == modelInstantiated) {\
				ssGetdX(model->S)[index] = (real_T)value[i];\
			} else {\
				allowed = fmi2False;			\
			}\
			break;															\
        case SFCN_FMI_BLOCKIO:												\
		    if (model->status == modelInstantiated) {					\
				setValue ## label(model->blockoutputs[index], dataType, value[i]);	\
			} else {														\
                allowed = fmi2False;											\
			}																\
			break;															\
		case SFCN_FMI_DWORK:												\
		    if (model->status == modelInstantiated) {					\
			    setValue ## label(model->dwork[index], dataType, value[i]);	\
			} else {														\
                allowed = fmi2False;											\
			}																\
			break;															\
		case SFCN_FMI_PARAMETER:											\
     		setValue ## label(model->parameters[index], dataType, value[i]);	\
			model->shouldRecompute = fmi2True;\
			paramChanged = fmi2True;\
			break;															\
        case SFCN_FMI_OUTPUT:												\
     		if (model->status == modelInstantiated) {					\
			    setValue ## label(model->outputs[index], dataType, value[i]);	\
			} else {														\
                allowed = fmi2False;											\
			}																\
			break;															\
		default:															\
	        logger(model, model->instanceName, fmi2Warning, "", "fmi2Set"#label": cannot set %u\n", r);		\
            return fmi2Warning;												\
		}																	\
		if (allowed == fmi2False) {											\
			logger(model, model->instanceName, fmi2Warning, "", "fmi2Set"#label": may not change %u at this stage\n", r);	\
			return fmi2Warning;												\
		}																	\
	}																		\
	if (paramChanged == fmi2True && SFCN_FMI_LOAD_MEX) {											\
		sfcn_fmi_copyToSFcnParams_(model->S);\
		sfcn_fmi_mxGlobalTunable_(model->S, 1, 1);\
	}\
	return fmi2OK

static fmi2Real getValueReal(const void* vPtr, int dataType)
{
	switch (dataType) {
		case SS_SINGLE:   /* real32_T  */
			return (fmi2Real) (*((real32_T*)vPtr));
		default:   /* All other cases treated as real_T */
			return (fmi2Real) (*((real_T*)vPtr));
	}
}

static fmi2Integer getValueInteger(const void* vPtr, int dataType)
{
	switch (dataType) {
		case SS_INT8:   /* int8_T  */
			return (fmi2Integer) (*((int8_T*)vPtr));
		case SS_UINT8:   /* uint8_T  */
			return (fmi2Integer) (*((uint8_T*)vPtr));
		case SS_INT16:   /* int16_T  */
			return (fmi2Integer) (*((int16_T*)vPtr));
		case SS_UINT16:   /* uint16_T  */
			return (fmi2Integer) (*((uint16_T*)vPtr));
		case SS_INT32:   /* int32_T  */
			return (fmi2Integer) (*((int32_T*)vPtr));
		case SS_UINT32:   /* uint32_T  */
			return (fmi2Integer) (*((uint32_T*)vPtr));
		default:   /* All other cases treated as int32_T */
			return (fmi2Integer) (*((int32_T*)vPtr));
	}
}

static fmi2Boolean getValueBoolean(const void* vPtr, int dataType)
{
	/* Should only be called if dataType is indeed boolean_T */
	return (fmi2Boolean) (*((boolean_T*)vPtr));
}

#define FMI_GET(label)												\
	Model* model = (Model*) c;											\
	size_t i;															\
																		\
	if (model->status == modelInstantiated) {                     \
		logger(model, model->instanceName, fmi2Warning, "", "fmi2Get"#label": Not allowed before call to fmi2EnterInitializationMode\n"); \
		return fmi2Warning;                                              \
	}																	\
																		\
	if (!model->isCoSim && !model->isDiscrete) {\
		if ((!isEqual(ssGetT(model->S), model->lastGetTime)) || model->shouldRecompute) {\
			sfcnOutputs(model->S,0);											\
			_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);			\
			if (ssGetmdlDerivatives(model->S) != NULL) {						\
				sfcnDerivatives(model->S);										\
			}																	\
			model->S->mdlInfo->simTimeStep = MINOR_TIME_STEP;					\
			model->shouldRecompute = fmi2False;\
			model->lastGetTime = ssGetT(model->S);\
		}\
	}\
																		\
	for (i = 0; i < nvr; i++) {											\
		const fmi2ValueReference r = vr[i];								\
		int index = SFCN_FMI_INDEX(r);									\
		int dataType = SFCN_FMI_DATATYPE(r);							\
																		\
		switch (SFCN_FMI_CATEGORY(r)) {									\
        case SFCN_FMI_INPUT:											\
			value[i] = getValue ## label(model->inputs[index], dataType);	\
			break;														\
		case SFCN_FMI_STATE:											\
			value[i] = (fmi2 ## label) ssGetContStates(model->S)[index];	\
			break;														\
		case SFCN_FMI_DERIVATIVE:										\
			value[i] = (fmi2 ## label) ssGetdX(model->S)[index];			\
			break;														\
        case SFCN_FMI_BLOCKIO:											\
		    value[i] = getValue ## label(model->blockoutputs[index], dataType);	\
			break;														\
		case SFCN_FMI_DWORK:											\
		    value[i] = getValue ## label(model->dwork[index], dataType);	\
			break;														\
		case SFCN_FMI_PARAMETER:										\
     		value[i] = getValue ## label(model->parameters[index], dataType);	\
			break;														\
        case SFCN_FMI_OUTPUT:											\
     		value[i] = getValue ## label(model->outputs[index], dataType);	\
			break;														\
		default:														\
			logger(model, model->instanceName, fmi2Warning, "", "fmi2Get"#label": cannot get %u\n", r);   \
            return fmi2Warning;											\
		}																\
	}																	\
	return fmi2OK;

/* ---------------------------------------------------------------------- */

fmi2Status fmi2SetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Real value[])
{
    FMI_SET(Real);
}

fmi2Status fmi2SetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer value[])
{
	FMI_SET(Integer);
}

fmi2Status fmi2SetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Boolean value[])
{
	FMI_SET(Boolean);
}

fmi2Status fmi2SetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2String  value[])
{
	FMI_UNSUPPORTED(SetString);
}

fmi2Status fmi2GetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Real value[])
{
	FMI_GET(Real);
}

fmi2Status fmi2GetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Integer value[])
{
	FMI_GET(Integer);
}

fmi2Status fmi2GetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Boolean value[])
{
	FMI_GET(Boolean);
}

fmi2Status fmi2GetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2String  value[])
{
	FMI_UNSUPPORTED(GetString);
}

fmi2Status fmi2GetFMUstate(fmi2Component c, fmi2FMUstate* FMUstate)
{
	FMI_UNSUPPORTED(GetFMUstate);
}

fmi2Status fmi2SetFMUstate(fmi2Component c, fmi2FMUstate FMUstate)
{
	FMI_UNSUPPORTED(SetFMUstate);
}

fmi2Status fmi2FreeFMUstate(fmi2Component c, fmi2FMUstate* FMUstate)
{
	FMI_UNSUPPORTED(FreeFMUstate);
}

fmi2Status fmi2SerializedFMUstateSize(fmi2Component c, fmi2FMUstate FMUstate, size_t* size)
{
	FMI_UNSUPPORTED(SerializedFMUstateSize);
}

fmi2Status fmi2SerializeFMUstate_     (fmi2Component c, fmi2FMUstate FMUstate, fmi2Byte serializedState[], size_t size)
{
	FMI_UNSUPPORTED(SerializeFMUstate);
}

fmi2Status fmi2DeSerializeFMUstate_   (fmi2Component c, const fmi2Byte serializedState[], size_t size, fmi2FMUstate* FMUstate)
{
	FMI_UNSUPPORTED(DeSerializeFMUstate);
}

fmi2Status fmi2GetDirectionalDerivative(fmi2Component c, const fmi2ValueReference vUnknown_ref[], size_t nUnknown,
                                                         const fmi2ValueReference vKnown_ref[], size_t nKnown,
                                                         const fmi2Real dvKnown[],
														       fmi2Real dvUnknown[])
{
	FMI_UNSUPPORTED(GetDirectionalDerivative);
}

/***************** Model Exchange functions *****************/

fmi2Status fmi2EnterEventMode(fmi2Component c)
{
	Model* model = (Model*) c;

	CHECK_INITIALIZED(model, "fmi2EnterEventMode");

	if (model->status != modelContinuousTimeMode) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2EnterEventMode: May only be called in continuous-time mode\n");
		return fmi2Warning;
	}

	logger(model, model->instanceName, fmi2OK, "", "Enter event mode at time = %.16f\n", ssGetT(model->S));
	model->status = modelEventMode;
	return fmi2OK;
}

fmi2Status fmi2NewDiscreteStates(fmi2Component c, fmi2EventInfo* eventInfo)
{
	int i;
	Model* model = (Model*) c;
	fmi2Real nextT;
	real_T compareVal;
	int_T sampleHit = 0;

	CHECK_INITIALIZED(model, "fmi2NewDiscreteStates");

#if defined(SFCN_FMI_VERBOSITY)
	logger(model, model->instanceName, fmi2OK, "", "fmi2NewDiscreteStates: Call at time = %.16f\n", ssGetT(model->S));
#endif

	/* Set sample hits for discrete systems */
	for (i=0;i<model->S->sizes.numSampleTimes;i++) {
		if (model->S->stInfo.sampleTimes[i] > SFCN_FMI_EPS) { /* Discrete sample time */
			if (i==0) {
				compareVal = model->nextHit_tid0; /* Purely discrete, use stored hit time for task 0 */
				model->isDiscrete = fmi2True;
			} else {
				compareVal = model->S->mdlInfo->t[i];
			}
			if (isEqual(ssGetT(model->S), compareVal)) {
				sampleHit = 1;
				model->S->mdlInfo->sampleHits[i] = 1;
#if defined(SFCN_FMI_VERBOSITY)
				logger(model, model->instanceName, fmi2OK, "", "fmi2NewDiscreteStates: Sample hit for task %d\n", i);
#endif
				/* Update time for next sample hit */
				model->numSampleHits[i]++;
			}
		}
	}
	/* Set sample hit for continuous sample time with FIXED_IN_MINOR_STEP_OFFSET */
	if (model->fixed_in_minor_step_offset_tid != -1) {
		/* Except first call after initialization */
		model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 0;
		if (model->hasEnteredContMode) {
			model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 1;
		}
	}
	if (SFCN_FMI_LOAD_MEX) {
		copyPerTaskSampleHits(model->S);
	}

	if (!(model->isDiscrete && !sampleHit)) { /* Do not evaluate model if purely discrete and no sample hit */
		model->S->mdlInfo->simTimeStep = MAJOR_TIME_STEP;
		sfcnOutputs(model->S, 0);
		_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);
		if (ssGetmdlUpdate(model->S) != NULL) {
#if defined(SFCN_FMI_VERBOSITY)
			logger(model, model->instanceName, fmi2OK, "", "fmi2NewDiscreteStates: Calling mdlUpdate at time %.16f\n", ssGetT(model->S));
#endif
			sfcnUpdate(model->S, 0);
		}
		model->S->mdlInfo->simTimeStep = MINOR_TIME_STEP;
	}

	/* Find next time event and reset sample hits */
	nextT = SFCN_FMI_MAX_TIME;
	for (i=0;i<model->S->sizes.numSampleTimes;i++) {
		if (model->S->stInfo.sampleTimes[i] > SFCN_FMI_EPS) { /* Discrete sample time */
			compareVal = model->S->stInfo.offsetTimes[i] + model->numSampleHits[i]*model->S->stInfo.sampleTimes[i];
			if (i==0) {
				/* Store, will be overwritten by fmiSetTime */
				model->nextHit_tid0 = compareVal;
			} else {
				model->S->mdlInfo->t[i] = compareVal;
			}
			if (compareVal < nextT) {
				nextT = compareVal;
			}
			model->S->mdlInfo->sampleHits[i] = 0;
		}
	}
	if (model->fixed_in_minor_step_offset_tid != -1) {
		model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 0;
	}
	if (SFCN_FMI_LOAD_MEX) {
		copyPerTaskSampleHits(model->S);
	}
	/* Only treat zero crossing functions for model exchange */
	if (!(model->isCoSim)) {
		if (model->S->modelMethods.sFcn.mdlZeroCrossings != NULL) {
			sfcnZeroCrossings(model->S);
		}
		for (i=0;i<SFCN_FMI_ZC_LENGTH;i++) {
			/* Store current ZC values at event */
			model->oldZC[i] = model->S->mdlInfo->solverInfo->zcSignalVector[i];
		}
	}
	model->shouldRecompute = fmi2True;

	eventInfo->newDiscreteStatesNeeded				= fmi2False;
    eventInfo->terminateSimulation					= fmi2False;
    eventInfo->nominalsOfContinuousStatesChanged	= fmi2False;
    eventInfo->valuesOfContinuousStatesChanged		= fmi2False;
#if defined(MATLAB_R2017b_)
	if (model->S->mdlInfo->mdlFlags.blockStateForSolverChangedAtMajorStep) {
		model->S->mdlInfo->mdlFlags.blockStateForSolverChangedAtMajorStep = 0U;
#else
	if (model->S->mdlInfo->solverNeedsReset == 1) {
		_ssClearSolverNeedsReset(model->S);
#endif
		eventInfo->valuesOfContinuousStatesChanged = fmi2True;
#if defined(SFCN_FMI_VERBOSITY)
		logger(model, model->instanceName, fmi2OK, "", "fmi2NewDiscreteStates: State values changed at time %.16f\n", ssGetT(model->S));
#endif
	}
	eventInfo->nextEventTimeDefined = (nextT < SFCN_FMI_MAX_TIME);
	eventInfo->nextEventTime = nextT;

#if defined(SFCN_FMI_VERBOSITY)
		logger(model, model->instanceName, fmi2OK, "", "fmi2NewDiscreteStates: Event handled at time = %.16f, next event time at %.16f\n", ssGetT(model->S), nextT);
#endif
	return fmi2OK;
}

fmi2Status fmi2EnterContinuousTimeMode(fmi2Component c)
{
	Model* model = (Model*) c;

	CHECK_INITIALIZED(model, "fmi2EnterContinuousTimeMode");

	if (model->status != modelEventMode) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2EnterContinuousTimeMode: May only be called in event mode\n");
		return fmi2Warning;
	}

	logger(model, model->instanceName, fmi2OK, "", "Enter continuous-time mode at time = %.16f\n", ssGetT(model->S));
	model->hasEnteredContMode = fmi2True;
	model->status = modelContinuousTimeMode;
	return fmi2OK;
}

fmi2Status fmi2CompletedIntegratorStep(fmi2Component c, fmi2Boolean noSetFMUStatePriorToCurrentPoint,
										fmi2Boolean* enterEventMode, fmi2Boolean* terminateSimulation)
{
	int i;
	real_T currZC_i;
	Model* model = (Model*) c;
	int rising  = 0;
	int falling = 0;

	CHECK_INITIALIZED(model, "fmi2CompletedIntegratorStep");

#if defined(SFCN_FMI_VERBOSITY)
	logger(model, model->instanceName, fmi2OK, "", "fmi2CompletedIntegratorStep: Call at time %.16f\n", ssGetT(model->S));
#endif

	/* Evaluate zero-crossing functions */
	if (model->S->modelMethods.sFcn.mdlZeroCrossings != NULL) {
		sfcnZeroCrossings(model->S);
	}
	/* Check for zero crossings */
	for (i=0;i<SFCN_FMI_ZC_LENGTH;i++) {
		currZC_i = model->S->mdlInfo->solverInfo->zcSignalVector[i];
		rising  = ((model->oldZC[i] < 0.0) && (currZC_i >= 0.0)) || ((model->oldZC[i] == 0.0) && (currZC_i > 0.0));
		falling = ((model->oldZC[i] > 0.0) && (currZC_i <= 0.0)) || ((model->oldZC[i] == 0.0) && (currZC_i < 0.0));
#if defined(SFCN_FMI_VERBOSITY)
		logger(model, model->instanceName, fmi2OK, "", "fmi2CompletedIntegratorStep: oldZC[%d] = %.16f ; currZC[%d] = %.16f\n", i, model->oldZC[i], i, currZC_i);
#endif
		if (rising || falling) {
			break;
		}
	}
	/* Store current zero-crossing values at step */
	for (i=0;i<SFCN_FMI_ZC_LENGTH;i++) {
		model->oldZC[i] = model->S->mdlInfo->solverInfo->zcSignalVector[i];
	}

	/* Do not set major time step if we stepped passed a zero crossing
	   Will be a major time step in EventMode */
	if (!(rising || falling) && !model->isDiscrete) {
		model->S->mdlInfo->simTimeStep = MAJOR_TIME_STEP;
		/* Update continuous task with FIXED_IN_MINOR_STEP_OFFSET */
		if (model->fixed_in_minor_step_offset_tid != -1) {
			model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 1;
			if (SFCN_FMI_LOAD_MEX) {
				copyPerTaskSampleHits(model->S);
			}
		}
		sfcnOutputs(model->S, 0);
		_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);
		if (ssGetmdlUpdate(model->S) != NULL) {
#if defined(SFCN_FMI_VERBOSITY)
			logger(model, model->instanceName, fmi2OK, "", "fmi2CompletedIntegratorStep: Calling mdlUpdate at time %.16f\n", ssGetT(model->S));
#endif
			sfcnUpdate(model->S, 0);
		}
		if (model->fixed_in_minor_step_offset_tid != -1) {
			model->S->mdlInfo->sampleHits[model->fixed_in_minor_step_offset_tid] = 0;
			if (SFCN_FMI_LOAD_MEX) {
				copyPerTaskSampleHits(model->S);
			}
		}
		model->S->mdlInfo->simTimeStep = MINOR_TIME_STEP;
	}
	*enterEventMode = fmi2False;
	*terminateSimulation = fmi2False;
	model->shouldRecompute=fmi2True;

	return fmi2OK;
}

fmi2Status fmi2SetTime(fmi2Component c, fmi2Real time)
{
	Model*  model = (Model*) c;

	model->S->mdlInfo->t[0] = time;
	if (model->fixed_in_minor_step_offset_tid != -1) {
		model->S->mdlInfo->t[model->fixed_in_minor_step_offset_tid] = time;
	}
	return fmi2OK;
}

fmi2Status fmi2SetContinuousStates(fmi2Component c, const fmi2Real x[], size_t nx)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;

	int_T nxS = ssGetNumContStates(model->S);
	if (((int_T) nx) != nxS) {
		status = fmi2Warning;
		logger(model, model->instanceName, status, "",
			"fmi2SetContinuousStates: argument nx = %u is incorrect, should be %u\n", nx, nxS);
		if (((int_T) nx) > nxS) {
			/* truncate */
			nx = nxS;
		}
	}
	memcpy(ssGetContStates(model->S), x, nx * sizeof(fmi2Real));
	model->shouldRecompute=fmi2True;

	return status;
}

fmi2Status fmi2GetDerivatives(fmi2Component c, fmi2Real derivatives[], size_t nx)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;
	int_T nxS;

	if (model->status == modelInstantiated) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2GetDerivatives: Not allowed before call to fmi2EnterInitializationMode\n");
		return fmi2Warning;
	}

	nxS = ssGetNumContStates(model->S);
	if (((int_T) nx) != nxS) {
		status = fmi2Warning;
		logger(model, model->instanceName, status, "",
			"fmi2GetDerivatives: argument nx = %u is incorrect, should be %u\n", nx, nxS);
		if (((int_T) nx) > nxS) {
			/* truncate */
			nx = nxS;
		}
	}

	sfcnOutputs(model->S,0);
	_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);
	if (ssGetmdlDerivatives(model->S) != NULL) {
		sfcnDerivatives(model->S);
		memcpy(derivatives, ssGetdX(model->S), nx * sizeof(fmi2Real));
	}
	model->S->mdlInfo->simTimeStep = MINOR_TIME_STEP;
	return status;
}

fmi2Status fmi2GetEventIndicators(fmi2Component c, fmi2Real eventIndicators[], size_t ni)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;
	int_T nzS;

	if (model->status == modelInstantiated) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2GetEventIndicators: Not allowed before call to fmi2EnterInitializationMode\n");
		return fmi2Warning;
	}

	nzS = SFCN_FMI_ZC_LENGTH;
	if (((int_T) ni) != nzS) {
		status = fmi2Warning;
		logger(model, model->instanceName, status, "",
			"fmi2GetEventIndicators: argument ni = %u is incorrect, should be %u\n", ni, nzS);
		if (((int_T) ni) > nzS) {
			/* truncate */
			ni = nzS;
		}
	}

	sfcnOutputs(model->S,0);
	_ssSetTimeOfLastOutput(model->S,model->S->mdlInfo->t[0]);
	if (model->S->modelMethods.sFcn.mdlZeroCrossings != NULL) {
		sfcnZeroCrossings(model->S);
		memcpy(eventIndicators, model->S->mdlInfo->solverInfo->zcSignalVector, ni * sizeof(fmi2Real));
	}
	model->S->mdlInfo->simTimeStep = MINOR_TIME_STEP;
	return status;
}

fmi2Status fmi2GetContinuousStates(fmi2Component c, fmi2Real states[], size_t nx)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;
	int_T nxS;

	if (model->status == modelInstantiated) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2GetContinuousStates: Not allowed before call to fmi2EnterInitializationMode\n");
		return fmi2Warning;
	}

	nxS = ssGetNumContStates(model->S);
	if (((int_T) nx) != nxS) {
		status = fmi2Warning;
		logger(model, model->instanceName, status, "",
			"fmi2GetContinuousStates: argument nx = %u is incorrect, should be %u\n", nx, nxS);
		if (((int_T) nx) > nxS) {
			/* truncate */
			nx = nxS;
		}
	}

	memcpy(states, ssGetContStates(model->S), nx * sizeof(fmi2Real));
	return status;
}

fmi2Status fmi2GetNominalsOfContinuousStates(fmi2Component c, fmi2Real x_nominal[], size_t nx)
{
	unsigned int i;
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;

	int_T nxS = ssGetNumContStates(model->S);
	if (((int_T) nx) != nxS) {
		status = fmi2Warning;
		logger(model, model->instanceName, status, "",
			"fmi2GetNominalContinuousStates: argument nx = %u is incorrect, should be %u\n", nx, nxS);
		if (((int_T) nx) > nxS) {
			/* truncate */
			nx = nxS;
		}
	}

	for (i=0;i<nx;i++) {
		x_nominal[i] = 1.0; /* Unknown nominal */
	}
	return status;
}


/***************** Co-Simulation functions *****************/

fmi2Status fmi2SetRealInputDerivatives(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer order[], const fmi2Real value[])
{
	Model* model = (Model*) c;
	size_t i;

	if (model->status <= modelInitializationMode) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2SetRealInputDerivatives: Slave is not initialized\n");
		return fmi2Warning;
	}
	if (nvr == 0 || nvr > SFCN_FMI_NBR_INPUTS) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2SetRealInputDerivatives: Invalid nvr = %d (number of inputs = %d)\n", nvr, SFCN_FMI_NBR_INPUTS);
		return fmi2Warning;
	}
	for (i = 0; i < nvr; i++) {
		const fmi2ValueReference r = vr[i];
		int index    = SFCN_FMI_INDEX(r);
		int dataType = SFCN_FMI_DATATYPE(r);

		if (order[i] == 1) {
			switch (SFCN_FMI_CATEGORY(r)) {
			case SFCN_FMI_INPUT:
				if (dataType == SS_DOUBLE) {
					/* Non-zero derivatives only for double-valued real inputs */
					model->inputDerivatives[index] = value[i];
#if defined(SFCN_FMI_VERBOSITY)
					logger(model, model->instanceName, fmi2OK, "", "fmi2SetRealInputDerivatives: Setting derivative at input #%d to %.16f at time = %.16f.\n", i, value[i], model->time);
#endif
				}
				break;
			default:
				logger(model, model->instanceName, fmi2Warning, "", "fmi2SetRealInputDerivatives: variable is not input");
				return fmi2Warning;
			}
		} else {
			logger(model, model->instanceName, fmi2Warning, "", "fmi2SetRealInputDerivatives: derivative order %d is not supported", order[i]);
			return fmi2Warning;
		}
	}
	model->derivativeTime = model->time;

	return fmi2OK;
}

fmi2Status fmi2GetRealOutputDerivatives(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer order[], fmi2Real value[])
{
	Model* model = (Model*) c;
	size_t i;

	if (model->status <= modelInitializationMode) {
        logger(model, model->instanceName, fmi2Warning, "", "fmi2GetRealOutputDerivatives: Slave is not initialized\n");
		return fmi2Warning;
	}
	if (nvr == 0 || nvr > SFCN_FMI_NBR_OUTPUTS) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2GetRealOutputDerivatives: Invalid nvr = %d (number of outputs = %d)\n", nvr, SFCN_FMI_NBR_OUTPUTS);
		return fmi2Warning;
	}
	for (i = 0; i < nvr; i++) {
		if (order[i] > 0) {
			value[i] = 0.0;
		} else {
			logger(model, model->instanceName, fmi2Warning, "", "fmi2GetRealOutputDerivatives: Derivative order 0 is not allowed\n");
			return fmi2Warning;
		}
	}
	return fmi2OK;
}

static void extrapolateInputs(Model* model, fmi2Real t)
{
	size_t i;
	fmi2Real dt = (t - model->derivativeTime);
	for (i = 0; i < SFCN_FMI_NBR_INPUTS; i++) {
		if (model->inputDerivatives[i] != 0.0) {
			*((real_T*)(model->inputs[i])) += model->inputDerivatives[i] * dt;
#if defined(SFCN_FMI_VERBOSITY)
			logger(model, model->instanceName, fmi2OK, "", "Extrapolated input #%d to value = %.16f\n", i, *((real_T*)(model->inputs[i])));
#endif
		}
	}
	model->derivativeTime = t;
}

fmi2Status fmi2DoStep(fmi2Component c, fmi2Real currentCommunicationPoint, fmi2Real communicationStepSize, fmi2Boolean noSetFMUStatePriorToCurrentPoint)
{
	Model* model = (Model*) c;
	fmi2Status status = fmi2OK;
	fmi2Real lastSolverTime, nextSolverTime;
	fmi2Real endStepTime;

	if (model->status <= modelInitializationMode) {
        logger(model, model->instanceName, fmi2Warning, "", "fmi2DoStep: Slave is not initialized\n");
		return fmi2Warning;
	}
	if (model->status == modelTerminated) {
        logger(model, model->instanceName, fmi2Warning, "", "fmi2DoStep: Slave terminated in previous step\n");
		return fmi2Warning;
	}
	if (!isEqual(model->time, currentCommunicationPoint)) {
		logger(model, model->instanceName, fmi2Warning, "", "fmi2DoStep: Invalid currentCommunicationPoint = %.16f, expected %.16f\n", currentCommunicationPoint, model->time);
		return fmi2Warning;
	}
	model->S->mdlInfo->simTimeStep = MAJOR_TIME_STEP;
	if (fabs(communicationStepSize) < SFCN_FMI_EPS) {
		/* Zero step size; External event iteration, just recompute outputs */
		sfcnOutputs(model->S, 0);
		return fmi2OK;
	}
	endStepTime = currentCommunicationPoint + communicationStepSize;
	lastSolverTime = model->nbrSolverSteps*SFCN_FMI_FIXED_STEP_SIZE;
	nextSolverTime = (model->nbrSolverSteps+1.0)*SFCN_FMI_FIXED_STEP_SIZE;
	while ( (nextSolverTime < endStepTime) || isEqual(nextSolverTime, endStepTime) ) {
#if defined(SFCN_FMI_VERBOSITY)
		logger(model, model->instanceName, fmi2OK, "", "fmi2DoStep: Internal solver step from %.16f to %.16f\n", lastSolverTime, nextSolverTime);
#endif
		/* Set time for state update */
		fmi2SetTime(c, lastSolverTime);
		/* Update continuous-time states */
		if (ssGetNumContStates(model->S) > 0) {
#if defined(SFCN_FMI_VERBOSITY)
			logger(model, model->instanceName, fmi2OK, "", "fmi2DoStep: Updating continuous states at time = %.16f.\n", model->S->mdlInfo->t[0]);
#endif
			/* Set ODE solver stop time */
			_ssSetSolverStopTime(model->S, nextSolverTime);
			/* Update states */
			rt_UpdateContinuousStates(model->S);
		}
		/* Set time for output calculations */
		fmi2SetTime(c, nextSolverTime);
		/* Extrapolate inputs */
		extrapolateInputs(model, nextSolverTime);
		/* Set sample hits and call mdlOutputs / mdlUpdate (always a discrete sample time = Fixed-step size) */
		UserData *userData = (UserData *)model->userData;
		fmi2NewDiscreteStates(c, &(userData->eventInfo));
		/* Update solver times */
		lastSolverTime = nextSolverTime;
		model->nbrSolverSteps++;
		nextSolverTime = (model->nbrSolverSteps+1.0)*SFCN_FMI_FIXED_STEP_SIZE;
	}
	model->time = endStepTime;

	return fmi2OK;
}

fmi2Status fmi2CancelStep(fmi2Component c)
{
	Model* model = (Model*) c;

	logger(model, model->instanceName, fmi2Discard, "", "fmi2CancelStep: Not supported since asynchronous execution of fmi2DoStep is not supported\n");
	return fmi2Discard;
}

fmi2Status fmi2GetStatus(fmi2Component c, const fmi2StatusKind s, fmi2Status*  value)
{
	Model* model = (Model*) c;

	logger(model, model->instanceName, fmi2Discard, "", "fmi2GetStatus: Not supported since asynchronous execution of fmi2DoStep is not supported\n");
	return fmi2Discard;
}

fmi2Status fmi2GetRealStatus(fmi2Component c, const fmi2StatusKind s, fmi2Real* value)
{
	Model* model = (Model*) c;

	if (s != fmi2LastSuccessfulTime) {
		logger(model, model->instanceName, fmi2Discard, "", "fmi2GetRealStatus: fmi2StatusKind %d unknown.", s);
		return fmi2Discard;
	}
	*value = model->time;
	return fmi2OK;
}

fmi2Status fmi2GetIntegerStatus(fmi2Component c, const fmi2StatusKind s, fmi2Integer* value)
{
	Model* model = (Model*) c;

	logger(model, model->instanceName, fmi2Discard, "", "fmi2GetIntegerStatus: fmi2StatusKind %d unknown.", s);
	return fmi2Discard;
}

fmi2Status fmi2GetBooleanStatus(fmi2Component c, const fmi2StatusKind s, fmi2Boolean* value)
{
	Model* model = (Model*) c;

	if (s != fmi2Terminated) {
		logger(model, model->instanceName, fmi2Discard, "", "fmi2GetBooleanStatus: fmi2StatusKind %d unknown.", s);
		return fmi2Discard;
	}
	*value = fmi2False;
	return fmi2OK;
}

fmi2Status fmi2GetStringStatus(fmi2Component c, const fmi2StatusKind s, fmi2String*  value)
{
	Model* model = (Model*) c;

	logger(model, model->instanceName, fmi2Discard, "",
			"fmi2GetStringStatus: not supported since asynchronous execution of fmi2DoStep is not supported.");
	return fmi2Discard;
}

/* ----------------- Local function definitions ----------------- */

static void logger(fmi2Component c, fmi2String instanceName, fmi2Status status,
				   fmi2String category, fmi2String message, ...)
{
	char buf[4096];
	va_list ap;
    int capacity;
	Model* model;

	if (c==NULL) return;

	model = (Model*) c;
	if (model->loggingOn == fmi2False && (status == fmi2OK || status == fmi2Discard)) {
		return;
	}

	va_start(ap, message);
    capacity = sizeof(buf) - 1;
#if defined(_MSC_VER) && _MSC_VER>=1400
	vsnprintf_s(buf, capacity, _TRUNCATE, message, ap);
#else
    buf[capacity]=0;
	vsnprintf(buf, capacity, message, ap);
#endif
	va_end(ap);
	fmi2ComponentEnvironment componentEnvironment = ((UserData *)model->userData)->functions.componentEnvironment;
	((UserData *)model->userData)->functions.logger(componentEnvironment, instanceName, status, category, buf);
}

/* FMU mapping of ssPrintf for child C source S-functions (through rtPrintfNoOp) */
int rtPrintfNoOp(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
    int capacity;

	if (currentModel==NULL) return -1;

	va_start(ap, fmt);
    capacity = sizeof(buf) - 1;
#if defined(_MSC_VER) && _MSC_VER>=1400
	vsnprintf_s(buf, capacity, _TRUNCATE, fmt, ap);
#else
    buf[capacity]=0;
	vsnprintf(buf, capacity, fmt, ap);
#endif
	va_end(ap);
	logger(currentModel, currentModel->instanceName, fmi2OK, "", buf);

    return 0;
}

/* Wrapper function to be called from C++ implementation of rtPrintfNoOp */
int rtPrintfNoOp_C(const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
    int capacity;

	if (currentModel==NULL) return -1;

	va_start(ap, fmt);
    capacity = sizeof(buf) - 1;
#if defined(_MSC_VER) && _MSC_VER>=1400
	vsnprintf_s(buf, capacity, _TRUNCATE, fmt, ap);
#else
    buf[capacity]=0;
	vsnprintf(buf, capacity, fmt, ap);
#endif
	va_end(ap);
	logger(currentModel, currentModel->instanceName, fmi2OK, "", buf);

    return 0;
}
