kernel void copy_buffer(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id];
}

kernel void heavy(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    for (int i = 0; i < 10; i++)
        for(int j = 0; j < 10; j++)
            output[id] = input[id]+i*j;
}