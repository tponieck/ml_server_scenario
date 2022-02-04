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
    header = ['threads','memory used by mem_bound_kernel','multiplier of duraton compute_bound_kernel','consumers','queries','queries per second']
    writer.writerow(header)
    f.close()

test_matrix = [[16, 32, 64, 128], [4, 16, 128, 1024], [0.5, 1, 4, 8], [8, 16], [1000], [2000, 4000]]

fill_header()
for t in test_matrix[0]:
    for mem in test_matrix[1]:
        for cbk_mul in test_matrix[2]:
            for s in test_matrix[3]:
                for q in test_matrix[4]:
                    for qps in test_matrix[5]:
                        result = run_test(str(t), str(mem), str(cbk_mul), str(s), str(q), str(qps))
                        data = [t, mem, cbk_mul, s, q, qps, result]
                        f = open('./result.csv', 'a', newline='')
                        writer = csv.writer(f)
                        writer.writerow(data)
                        f.close()