kernel void copy_buffer(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id];
    //printf("mem bound output: %c\n", output[id]);
}

kernel void add_buffers(const global char *input, const global char *input2, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id] + input2[id];
    //printf("mem bound output: %c\n", output[id]);
}

kernel void mul_buffers(const global char *input, const global char *input2, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id] * input2[id];
    //printf("mem bound output: %c\n", output[id]);
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
        a += i + i;
        //a = a % 100;

    }
    output[id] = a;
    //if (id == 0)
    //printf("cmp bound output: %d   %d\n", output[id], counter);
}

kernel void mem_bound_kernel(const global char *input, const global char *input2, global char *output, int counter)
{
    const size_t id = get_global_id(0);
    for (int i = 0; i < counter; i++)
    {
        output[id] += input[id] + input2[id];
    }
    //if (id == 0)
    //printf("mem bound output: %d\n", output[0]);
}

