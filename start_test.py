import subprocess

def run_test(t, mem, cbk_mul, s , q, qps):
    command = "./sand_box --disable_wu --single_ccs --resnet --t " + t + " --mem " + mem + " --cbk_mul " + cbk_mul + " --s " + s +" --q " + q + " --qps " + qps
    process = subprocess.Popen(command.split(), stdout=subprocess.PIPE)
    output, error = process.communicate()
    process.wait()

test_matrix = [[16, 32, 64, 128], [4, 16, 128, 1024], [0.5, 1, 4, 8], [8, 16], [1], [2000, 4000]]


for t in test_matrix[0]:
    for mem in test_matrix[1]:
        for cbk_mul in test_matrix[2]:
            for s in test_matrix[3]:
                for q in test_matrix[4]:
                    for qps in test_matrix[5]:
                        run_test(str(t), str(mem), str(cbk_mul), str(s), str(q), str(qps))
                        print(str(t) + str(mem) + str(cbk_mul) + str(s) + str(q) + str(qps) + "\n")
                












#process = subprocess.Popen(command.split(), stdout=subprocess.PIPE)
#output, error = process.communicate()