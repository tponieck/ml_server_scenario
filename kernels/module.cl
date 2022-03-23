kernel void copy_buffer(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id];
}

kernel void add_buffers(const global char *input, const global char *input2, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id] + input2[id];
}

kernel void mul_buffers(const global char *input, const global char *input2, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id] * input2[id];
}

kernel void heavy(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    for (int i = 0; i < 10; i++)
        for(int j = 0; j < 10; j++)
            output[id] = input[id]+i*j;
}

kernel void cmp_bound_kernel(const global char *input, const global char *input2, global char *output, int counter)
{
    int a = 1;
    const size_t id = get_global_id(0);
    for (int i = 1; i <= counter; i++)
    {
        a += i * i;
    }
    output[id] = a;
}

kernel void mem_bound_kernel(const global char *input, const global char *input2, global char *output, int threadsize)
{
    const size_t id = get_global_id(0);
    for (int i = 0; i < input; i++)
    {
        output[i * threadsize + id] += input[i * threadsize + id] + input2[i * threadsize + id];
    }
}

kernel void set_n_to_output(const global char *input, const global char *input2, global char *output, int n)
{
    const size_t id = get_global_id(0);
    output[id] = n;
}

