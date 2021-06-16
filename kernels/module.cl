kernel void copy_buffer(const global char *input, global char *output)
{
    const size_t id = get_global_id(0);
    output[id] = input[id];
}