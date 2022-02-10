import subprocess
import csv

def run_test(t, mem, cbk_mul, s , q, qps):
    command = "./sand_box --disable_wu --single_ccs --resnet --t " + t + " --mem " + mem + " --cbk_mul " + cbk_mul + " --s " + s +" --q " + q + " --qps " + qps
    process = subprocess.Popen(command.split(), stdout=subprocess.PIPE)
    output = str(process.communicate())
    process.wait()
    return output.split(":", 1)[1].split("\\", 1)[0]

def fill_header():
    f = open('./result.csv', 'a', newline='')
    writer = csv.writer(f)
    header = ['threads','memory used by mem_bound_kernel','multiplier of duraton compute_bound_kernel','consumers','queries','queries per second','result [ms]']
    writer.writerow(header)
    f.close()
    
threads = [16, 32, 64, 128]
memory = [4, 16, 128, 1024]
compute_bound_kernel_multiplier = [0.5, 1, 4, 8]
consumers = [8, 16]
queries = 1000
queries_pers_second = [2000, 4000]

fill_header()
for t in threads:
    for mem in memory:
        for cbk_mul in compute_bound_kernel_multiplier:
            for s in consumers:
                for qps in queries_pers_second:
                    result = run_test(str(t), str(mem), str(cbk_mul), str(s), str(queries), str(qps))
                    data = [t, mem, cbk_mul, s, queries, qps, result]
                    f = open('./result.csv', 'a', newline='')
                    writer = csv.writer(f)
                    writer.writerow(data)
                    f.close()