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
    long int a = 1;
    for (int i = 0; i < counter; i++)
    {
        a = i * a * a;

    }
    output[0] = a;
}

kernel void mem_bound_kernel(const global char *input, const global char *input2, global char *output, int counter)
{
    for (int i = 0; i < counter; i++)
    {
        output[i] = input[i] + input2[i];
    }
}

