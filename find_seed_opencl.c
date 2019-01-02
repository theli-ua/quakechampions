#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
 
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
 
#define MAX_SOURCE_SIZE (0x100000)

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t unk;
    uint64_t central_entries;
    uint64_t central_size;
    uint64_t central_offset;
    uint64_t dummy_offset;
} main_header_t;
#pragma pack(pop)

const static uint32_t magic = 0x06064b50;
#define BUFFER_SIZE 0x4000

int main(int argc, const char* argv[]) {
    (void)argc;
    const char* fname = argv[1];
    FILE *fp;
    fp = fopen(fname, "rb");
    uint8_t *footer = (uint8_t*)malloc(BUFFER_SIZE);

    fseek(fp,-BUFFER_SIZE, SEEK_END);
    fread(footer, 1, BUFFER_SIZE, fp);

    main_header_t *header = (main_header_t*)memmem(footer, BUFFER_SIZE, &magic, sizeof(magic));
    if (!header) {
        printf("Could not find footer\n");
        exit(1);
    }
    uint8_t *buffer = (uint8_t*)malloc(BUFFER_SIZE);
    uint8_t *key = footer + (BUFFER_SIZE - 40);
    fseek(fp, header->central_offset, SEEK_SET);
    fread(buffer, 1, BUFFER_SIZE, fp);

    uint8_t *SEED_RESULTS = (uint8_t*)calloc(1, 0xFFFFFFFF / 8);
 
    // Load the kernel source code into the array source_str
    char *source_str;
    size_t source_size;
 
    fp = fopen("find_seed_opencl.cl", "r");
    if (!fp) {
        fprintf(stderr, "Failed to load kernel.\n");
        exit(1);
    }
    source_str = (char*)malloc(MAX_SOURCE_SIZE);
    source_size = fread( source_str, 1, MAX_SOURCE_SIZE, fp);
    fclose( fp );
 
    // Get platform and device information
    cl_platform_id platform_id = NULL;
    cl_device_id device_id = NULL;   
    cl_uint ret_num_devices;
    cl_uint ret_num_platforms;
    cl_int ret = clGetPlatformIDs(1, &platform_id, &ret_num_platforms);
    ret = clGetDeviceIDs( platform_id, CL_DEVICE_TYPE_GPU, 1, 
            &device_id, &ret_num_devices);
 
    size_t workgroup_size;
    clGetDeviceInfo(device_id, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(workgroup_size), &workgroup_size, NULL);
    printf("Device max workgroup size:%zd\n", workgroup_size);
    
    size_t max_compute_units;
    clGetDeviceInfo(device_id, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(workgroup_size), &max_compute_units, NULL);
    printf("Device max compute units:%zd\n", workgroup_size);

    // Create an OpenCL context
    cl_context context = clCreateContext( NULL, 1, &device_id, NULL, NULL, &ret);
 
    // Create a command queue
    cl_command_queue command_queue = clCreateCommandQueue(context, device_id, 0, &ret);
 
    // Create memory buffers on the device for each vector 
    cl_mem key_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, 
            32, NULL, &ret);
    cl_mem input_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY,
            BUFFER_SIZE, NULL, &ret);
    cl_mem seeds_mem_obj = clCreateBuffer(context, CL_MEM_READ_WRITE, 
            0xFFFFFFFF / 8, NULL, &ret);
 
    ret = clEnqueueWriteBuffer(command_queue, key_mem_obj, CL_TRUE, 0,
            32, key, 0, NULL, NULL);
    ret = clEnqueueWriteBuffer(command_queue, input_mem_obj, CL_TRUE, 0, 
            BUFFER_SIZE, buffer, 0, NULL, NULL);



    ret = clEnqueueWriteBuffer(command_queue, seeds_mem_obj, CL_TRUE, 0, 
            0xFFFFFFFF / 8, SEED_RESULTS, 0, NULL, NULL);
 
    // Create a program from the kernel source
    cl_program program = clCreateProgramWithSource(context, 1, 
            (const char **)&source_str, (const size_t *)&source_size, &ret);
 
    // Build the program
    ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);
    if (ret != CL_SUCCESS) {
        size_t len = 0;
        ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
        char *buffer = (char*)calloc(len, sizeof(char));
        ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, len, buffer, NULL);
        printf("Error building program:\n%s", buffer);
        free(buffer);
        exit(1);
    }
 
    // Create the OpenCL kernel
    cl_kernel kernel = clCreateKernel(program, "find_seed", &ret);
 
    // Set the arguments of the kernel
    ret = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&key_mem_obj);
    ret = clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&input_mem_obj);
    ret = clSetKernelArg(kernel, 2, sizeof(cl_mem), (void *)&seeds_mem_obj);
 
    // Execute the OpenCL kernel on the list
    /*for (uint64_t seed = 0x6F01BCCC; seed < 0xFFFFFFFF; seed += workgroup_size) {*/
    size_t global_item_size = workgroup_size;
    size_t local_item_size = 32;
#define SEEDS_PER_ITERATION 128
    for (uint64_t seed = 0; seed < 0xFFFFFFFF / SEEDS_PER_ITERATION ; seed += workgroup_size) {
        size_t global_offset = seed;
        ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, &global_offset, 
                &global_item_size, &local_item_size, 0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            printf("Error sending work to GPU ret: %d\n", ret);
        }
        printf("\rProgress %f%%", (double)seed * 100 * SEEDS_PER_ITERATION / 0xFFFFFFFF);

        fflush(stdout);
        /*break;*/
        // Display the result to the screen
 
    }
    printf("\n");
        
    // Read the memory buffer C on the device to the local variable C
    ret = clEnqueueReadBuffer(command_queue, seeds_mem_obj, CL_TRUE, 0, 
            0xFFFFFFFF / 8, SEED_RESULTS, 0, NULL, NULL);
 
    for(unsigned int i = 0; i < 0xFFFFFFFF / 8; i++) {
        if (SEED_RESULTS[i]) {
            for (int subi = 0; subi < 8; ++subi) {
                if (SEED_RESULTS[i] & (1 << subi))
                    printf("Found seed: 0x%X\n", i * 8 + subi);
            }
            /*goto done;*/
        }
    }

 
    // Clean up
    ret = clFlush(command_queue);
    ret = clFinish(command_queue);
    ret = clReleaseKernel(kernel);
    ret = clReleaseProgram(program);
    ret = clReleaseMemObject(seeds_mem_obj);
    ret = clReleaseMemObject(input_mem_obj);
    ret = clReleaseMemObject(key_mem_obj);
    ret = clReleaseCommandQueue(command_queue);
    ret = clReleaseContext(context);
    return 0;
}
