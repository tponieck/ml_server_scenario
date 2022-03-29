kernel void copy_buffer(const global char *input, global char *output, int threads, int input_size)
{
    const size_t id = get_global_id(0);
    output[id] = input[id];
}
__attribute__((reqd_work_group_size(32, 1, 1)))
__attribute__((intel_reqd_sub_group_size(16)))
kernel void add_buffers(const global char *input, const global char *input2, global char *output, int threads, int input_size)
{
	
	const size_t id = get_group_id(0)*2 + get_sub_group_id();
    if(get_sub_group_local_id() == 0){
        for (int j = 0; j < input_size/threads;j++){
            int a = 1;
            int in1 = input[id + j*threads];
            int in2 = input2[id + j*threads];
            for (int i = 1; i <= 123 * 1; i++)
                {
                    a += in1 + in2;
                }
            output[id + j*threads] = a;
        }
    }
}

kernel void mul_buffers(const global char *input, const global char *input2, global char *output, int threads, int input_size)
{
    const size_t id = get_global_id(0);
    output[id] = input[id] * input2[id];
}

kernel void heavy(const global char *input, global char *output, int threads, int input_size)
{
    const size_t id = get_global_id(0);
    for (int i = 0; i < 10; i++)
        for(int j = 0; j < 10; j++)
            output[id] = input[id]+i*j;
}

__attribute__((reqd_work_group_size(32, 1, 1)))
__attribute__((intel_reqd_sub_group_size(16)))
kernel void cmp_bound_kernel(const global char *input, const global char *input2, global char *output, int counter, int threads, int input_size)
{
    
    const size_t id = get_group_id(0)*2 + get_sub_group_id();
    if(get_sub_group_local_id() == 0){
        for (int j = 0; j < input_size/threads;j++){
            int a = 1;
            int in1 = input[id + j*threads];
            int in2 = input2[id + j*threads];
            for (int i = 1; i <= counter; i++)
            {
                a += in1 + in2;
            }
            output[id + j*threads] = a;
        }
    }
    
}

__attribute__((reqd_work_group_size(32, 1, 1)))
__attribute__((intel_reqd_sub_group_size(16)))
kernel void mem_bound_kernel(const global char *input, const global char *input2, global char *output, int counter, int threads, int input_size)
{
    uint a = 0;
    uint b = 0;
    const size_t id = get_group_id(0)*2 + get_sub_group_id();
    if(get_sub_group_local_id() == 0){
        for (int j = 0; j < input_size/threads;j++){
            for (int i = 0; i < counter; i++)
            {
                int in1 = input[id + j*threads + counter*input_size];
                int in2 = input2[id + j*threads + counter*input_size];
                a += in1;
                b += in2;
            }
        
            output[id + j*threads + counter*input_size] = a + b;
        }
    }
}

kernel void set_n_to_output(const global char *input, const global char *input2, global char *output, int n, int threads, int input_size)
{
    const size_t id = get_global_id(0);
    output[id] = n;
}

