#include "config.h"

#include "../h264_types.h"
#include "../h264_parser.h"
#include "../h264_nal.h"
#include "../h264_entropy.h"
#include "../h264_rec.h"
#include "../h264_misc.h"
#include "../h264_pred_mode.h"

#include <sys/stat.h>
#include <assert.h>
#include <pthread.h>
#include "h264_opencl.h"
#include <unistd.h>
#include "h264_mc_opencl.h"
#include "h264_idct_opencl.h"
cl_platform_id cpPlatform;
cl_uint uiNumDevices;
cl_device_id* cdDevices;
cl_context cxGPUContext;
cl_event ceEvent;               // OpenCL event

int curslice=0;
int widthindex=0;
int heightindex=0;
int mb_height = 0;
int mb_width  = 0;
int openclonline=0;

int av_exit(int ret){
	#undef exit
	exit(ret);
	return ret;
}

#define PRINTMATRIX(type) \
void printmatrix##type(type* dst,int size,int stride)	\
{														\
	for(int ss=0;ss<size;ss++)						\
	{												\
		printf("\n");								\
		for(int tt=0;tt<size;tt++)					\
		{											\
			printf("\t%d",dst[ss*stride+tt]);		\
		}											\
	}												\
	printf("\n curslice:%d,widthindex:%d,heightindex:%d\n",curslice,widthindex,heightindex);\
}

PRINTMATRIX(short)
PRINTMATRIX(int)
PRINTMATRIX(uint8_t)

int64_t diff(struct timespec start,struct timespec end)
{
	struct timespec temp;
	int64_t d;
	if ((end.tv_nsec-start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec-start.tv_sec-1;
		temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	d = temp.tv_sec*1000000000+temp.tv_nsec;
	return d;
}

const char* oclErrorString(cl_int error)
{
    static const char* errorString[] = {
        "CL_SUCCESS",
        "CL_DEVICE_NOT_FOUND",
        "CL_DEVICE_NOT_AVAILABLE",
        "CL_COMPILER_NOT_AVAILABLE",
        "CL_MEM_OBJECT_ALLOCATION_FAILURE",
        "CL_OUT_OF_RESOURCES",
        "CL_OUT_OF_HOST_MEMORY",
        "CL_PROFILING_INFO_NOT_AVAILABLE",
        "CL_MEM_COPY_OVERLAP",
        "CL_IMAGE_FORMAT_MISMATCH",
        "CL_IMAGE_FORMAT_NOT_SUPPORTED",
        "CL_BUILD_PROGRAM_FAILURE",
        "CL_MAP_FAILURE",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "CL_INVALID_VALUE",
        "CL_INVALID_DEVICE_TYPE",
        "CL_INVALID_PLATFORM",
        "CL_INVALID_DEVICE",
        "CL_INVALID_CONTEXT",
        "CL_INVALID_QUEUE_PROPERTIES",
        "CL_INVALID_COMMAND_QUEUE",
        "CL_INVALID_HOST_PTR",
        "CL_INVALID_MEM_OBJECT",
        "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR",
        "CL_INVALID_IMAGE_SIZE",
        "CL_INVALID_SAMPLER",
        "CL_INVALID_BINARY",
        "CL_INVALID_BUILD_OPTIONS",
        "CL_INVALID_PROGRAM",
        "CL_INVALID_PROGRAM_EXECUTABLE",
        "CL_INVALID_KERNEL_NAME",
        "CL_INVALID_KERNEL_DEFINITION",
        "CL_INVALID_KERNEL",
        "CL_INVALID_ARG_INDEX",
        "CL_INVALID_ARG_VALUE",
        "CL_INVALID_ARG_SIZE",
        "CL_INVALID_KERNEL_ARGS",
        "CL_INVALID_WORK_DIMENSION",
        "CL_INVALID_WORK_GROUP_SIZE",
        "CL_INVALID_WORK_ITEM_SIZE",
        "CL_INVALID_GLOBAL_OFFSET",
        "CL_INVALID_EVENT_WAIT_LIST",
        "CL_INVALID_EVENT",
        "CL_INVALID_OPERATION",
        "CL_INVALID_GL_OBJECT",
        "CL_INVALID_BUFFER_SIZE",
        "CL_INVALID_MIP_LEVEL",
        "CL_INVALID_GLOBAL_WORK_SIZE",
    };

    const int errorCount = sizeof(errorString) / sizeof(errorString[0]);

    const int index = -error;

    return (index >= 0 && index < errorCount) ? errorString[index] : "Unspecified Error";
}

void __oclCheckErrorEX(cl_int iSample, cl_int iReference, void (*pCleanup)(int), const char* cFile, const int iLine)
{
    // An error condition is defined by the sample/test value not equal to the reference
    if (iReference != iSample)
    {
        // If the sample/test value isn't equal to the ref, it's an error by defnition, so override 0 sample/test value
        iSample = (iSample == 0) ? -9999 : iSample;

        // Log the error info
        printf("\n !!! Error # %i (%s) at line %i , in file %s !!!\n\n", iSample, oclErrorString(iSample), iLine, cFile);

        // Cleanup and exit, or just exit if no cleanup function pointer provided.  Use iSample (error code in this case) as process exit code.
        if (pCleanup != NULL)
        {
            pCleanup(iSample);
        }
        else
        {
            printf("Exiting...\n");
            av_exit(iSample);
        }
    }
}

cl_int oclGetPlatformID(cl_platform_id* clSelectedPlatformID)
{
    char chBuffer[1024];
    cl_uint num_platforms;
    cl_platform_id* clPlatformIDs;
    cl_int ciErrNum;
    *clSelectedPlatformID = NULL;

    // Get OpenCL platform count
    ciErrNum = clGetPlatformIDs (0, NULL, &num_platforms);
    if (ciErrNum != CL_SUCCESS)
    {
        printf(" Error %i in clGetPlatformIDs Call !!!\n\n", ciErrNum);
        return -1000;
    }
    else
    {
        if(num_platforms == 0)
        {
            printf("No OpenCL platform found!\n\n");
            return -2000;
        }
        else
        {
            // if there's a platform or more, make space for ID's
            if ((clPlatformIDs = (cl_platform_id*)av_malloc(num_platforms * sizeof(cl_platform_id))) == NULL)
            {
                printf("Failed to allocate memory for cl_platform ID's!\n\n");
                return -3000;
            }

            // get platform info for each platform and trap the NVIDIA platform if found
            ciErrNum = clGetPlatformIDs (num_platforms, clPlatformIDs, NULL);
            for(cl_uint i = 0; i < num_platforms; ++i)
            {
                ciErrNum = clGetPlatformInfo (clPlatformIDs[i], CL_PLATFORM_NAME, 1024, &chBuffer, NULL);
                if(ciErrNum == CL_SUCCESS)
                {
                    if(strstr(chBuffer, "NVIDIA") != NULL)
                    {
		        printf("Used platform %d is %s\n",i,chBuffer);
                        *clSelectedPlatformID = clPlatformIDs[i];
                        break;
                    }
		    if(strstr(chBuffer,"AMD")!=NULL)
		    {
			printf("Used platform %d is %s\n",i,chBuffer);		      
		        *clSelectedPlatformID = clPlatformIDs[i];
			break;
		    }
                }
            }

            // default to zeroeth platform if NVIDIA not found
            if(*clSelectedPlatformID == NULL)
            {
                *clSelectedPlatformID = clPlatformIDs[0];
            }
            av_free(clPlatformIDs);
        }
    }

    return CL_SUCCESS;
}

char* readSource(const char *sourceFilename, size_t *psize) {

#define fplog stderr
   FILE *fp;
   int err;
   int size;

   char *source;

   fp = fopen(sourceFilename, "rb");
   if(fp == NULL) {
      fprintf(fplog,"Could not open kernel file: %s\n", sourceFilename);
      exit(-1);
   }

   err = fseek(fp, 0, SEEK_END);
   if(err != 0) {
      fprintf(fplog,"Error seeking to end of file\n");
      exit(-1);
   }

   size = ftell(fp);
   if(size < 0) {
      fprintf(fplog,"Error getting file position\n");
      exit(-1);
   }

   err = fseek(fp, 0, SEEK_SET);
   if(err != 0) {
      fprintf(fplog,"Error seeking to start of file\n");
      exit(-1);
   }

   source = (char*)av_malloc(size+1);
   if(source == NULL) {
      fprintf(fplog,"Error allocating %d bytes for the program source\n", size+1);
      exit(-1);
   }

   err = fread(source, 1, size, fp);
   if(err != size) {
      fprintf(fplog,"only read %d bytes\n", err);
      exit(0);
   }
#undef fplog
   source[size] = '\0';
   *psize = size;
   return source;
}

static int isrebuild(const char *KernelFilename){
    cl_int ciErrNum;
    char sourceFile[100];
    char binaryFile[100];
    struct stat64 BufFile;
    struct stat64 BufBin;
    int rebuild = 0;
    int RtFile, RtBin;
    size_t filesize =0;
    strcpy(sourceFile,KernelFilename);
    strcpy(binaryFile,KernelFilename);
    strcat(binaryFile,"bin");
  
    RtFile= stat64(KernelFilename,&BufFile);
    RtBin = stat64(binaryFile, &BufBin);
    if (RtFile!=0){
        fprintf(stderr, "Could not find the kernel source file %s\n", sourceFile);
        exit(-1);
    }
    if (RtBin!=0){
        FILE *myFile ;
        myFile = fopen(binaryFile, "rb");
        if(myFile==NULL){
            rebuild = 1;
	}
    }else{
    //compare the time stamp of binary file and source file 
        time_t fileSourceModifiedDate = BufFile.st_mtime;
        time_t fileBinModifiedDate    = BufBin.st_mtime;
        if(fileSourceModifiedDate >fileBinModifiedDate)
          rebuild = 1;
    }
    return rebuild;
}
void BuildProgram(const char *KernelFilename, cl_program *CreatedProgram){
    cl_int ciErrNum;
    int rebuild = isrebuild(KernelFilename);
    size_t filesize=0;
    char binaryFile[100];
    strcpy(binaryFile,KernelFilename);
    strcat(binaryFile,"bin");
    if(openclonline)
        rebuild = 1;
    printf(rebuild?"Online":"offline");
    printf(" rebuilding %s...\n", KernelFilename);
    if(rebuild){
        char *loadedSource = readSource(KernelFilename, &filesize);
        oclCheckError(loadedSource!= NULL, TRUE);
        *CreatedProgram = clCreateProgramWithSource(cxGPUContext, 1, (const char **)&loadedSource, NULL, &ciErrNum);
        ciErrNum = clBuildProgram(*CreatedProgram, 0, NULL,"-cl-mad-enable -I ../h264dec/libavcodec/",NULL, NULL);
        oclCheckError(ciErrNum, CL_SUCCESS);
        av_free(loadedSource);
        if(openclonline)
            return;
	//Cache the Binary code
	oclLogPtx(*CreatedProgram, cdDevices[0], binaryFile);
    }else{
	const char *loadedBinary = readSource(binaryFile, &filesize);
	oclCheckError(loadedBinary!= NULL, TRUE);    
	*CreatedProgram = clCreateProgramWithBinary(cxGPUContext, 1, cdDevices, &filesize, (const unsigned char **)&loadedBinary, NULL, &ciErrNum);
	oclCheckError(ciErrNum, CL_SUCCESS);
	ciErrNum = clBuildProgram(*CreatedProgram, 1, &cdDevices[0],"-cl-mad-enable -I ../h264dec/libavcodec/",NULL, NULL);
	oclCheckError(ciErrNum, CL_SUCCESS);
	av_free(loadedBinary);
    }
    if (ciErrNum != CL_SUCCESS){
	// write out standard error, Build Log and PTX, then return error
	oclLogBuildInfo(*CreatedProgram, cdDevices[0]);
	oclCheckError(ciErrNum, CL_SUCCESS);
    }
}
void oclLogBuildInfo(cl_program opencl_program, cl_device_id cdDevice)
{

    // write out the build log and ptx, then exit
    char cBuildLog[10240];
    clGetProgramBuildInfo(opencl_program, cdDevice, CL_PROGRAM_BUILD_LOG,
                          sizeof(cBuildLog), cBuildLog, NULL );
    int  i=0;
    while(cBuildLog[i++]!='\0');
    cBuildLog[i]='\0';
    printf("\n%s\nBuild Log:\n%s\n%s\n", HDASHLINE, cBuildLog, HDASHLINE);
}

void oclLogPtx(cl_program opencl_program, cl_device_id cdDevice, const char* cPtxFileName)
{
    // Grab the number of devices associated with the program
    cl_uint num_devices;
    clGetProgramInfo(opencl_program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, NULL);

    // Grab the device ids
    cl_device_id* devices = (cl_device_id*) av_malloc(num_devices * sizeof(cl_device_id));
    clGetProgramInfo(opencl_program, CL_PROGRAM_DEVICES, num_devices * sizeof(cl_device_id), devices, 0);

    // Grab the sizes of the binaries
    size_t* binary_sizes = (size_t*)av_malloc(num_devices * sizeof(size_t));
    clGetProgramInfo(opencl_program, CL_PROGRAM_BINARY_SIZES, num_devices * sizeof(size_t), binary_sizes, NULL);

    // Now get the binaries
    char** ptx_code = (char**)av_malloc(num_devices * sizeof(char*));
    for( unsigned int i=0; i<num_devices; ++i)
    {
        ptx_code[i] = (char*)av_malloc(binary_sizes[i]);
    }
    clGetProgramInfo(opencl_program, CL_PROGRAM_BINARIES, sizeof(char *)*num_devices, ptx_code, NULL);

    // Find the index of the device of interest
    unsigned int idx = 0;
    while((idx < num_devices) && (devices[idx] != cdDevice))
    {
        ++idx;
    }

    // If the index is associated, log the result
    if(idx < num_devices)
    {

        // if a separate filename is supplied, dump ptx there
        if (NULL != cPtxFileName)
        {
            printf("\nWriting ptx to separate file: %s ...\n\n", cPtxFileName);
            FILE* pFileStream = NULL;
            #ifdef _WIN32
                fopen_s(&pFileStream, cPtxFileName, "wb");
            #else
                pFileStream = fopen(cPtxFileName, "wb");
            #endif

            fwrite(ptx_code[idx], binary_sizes[idx], 1, pFileStream);
            fclose(pFileStream);
        }
        else // log to logfile and console if no ptx file specified
        {
           printf("\n%s\nProgram Binary:\n%s\n%s\n", HDASHLINE, ptx_code[idx], HDASHLINE);
        }
    }

    // Cleanup
    av_free(devices);
    av_free(binary_sizes);
    for(unsigned int i = 0; i < num_devices; ++i)
    {
        av_free(ptx_code[i]);
    }
    av_free( ptx_code );
}

int gpu_opencl_construct_environment(H264Context *h)
{
    cl_int ciErrNum;
    
    //Get the platform
    ciErrNum = oclGetPlatformID(&cpPlatform);
    oclCheckError(ciErrNum, CL_SUCCESS);

    //Get the devices
    ciErrNum = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_GPU, 0, NULL, &uiNumDevices);
    oclCheckError(ciErrNum, CL_SUCCESS);
    cdDevices = (cl_device_id *)av_malloc(uiNumDevices * sizeof(cl_device_id) );
    ciErrNum = clGetDeviceIDs(cpPlatform, CL_DEVICE_TYPE_GPU, uiNumDevices, cdDevices, NULL);
    oclCheckError(ciErrNum, CL_SUCCESS);      
    //Create the context
    cxGPUContext = clCreateContext(0, uiNumDevices, cdDevices, NULL, NULL, &ciErrNum);
    oclCheckError(ciErrNum, CL_SUCCESS);
    // Program Setup
	if(gpumode==GPU_IDCT||gpumode==GPU_TOTAL)
		idct_opencl_construct_env(h);
	if(gpumode==GPU_MC_NOVERLAP||gpumode==GPU_MC||gpumode==GPU_TOTAL)
		mc_opencl_construct_env(h);
    return 0;
}

int gpu_opencl_destroy_environment(void)
{
	cl_int ciErrNum=0;

	if(gpumode==GPU_IDCT||gpumode==GPU_TOTAL)
		idct_opencl_destruct_env();
	if(gpumode==GPU_MC_NOVERLAP||gpumode==GPU_MC||gpumode==GPU_TOTAL)
		mc_opencl_destruct_env();
	// cleanup OpenCL objects
	if(ceEvent)
		clReleaseEvent(ceEvent);
    ciErrNum |= clReleaseContext(cxGPUContext);
    oclCheckError(ciErrNum, CL_SUCCESS);
    av_free(cdDevices);
	return 0;
}

