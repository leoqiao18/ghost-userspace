import sys

import matplotlib.pyplot as plt

def plot_bench_graph(sched_type, interval):
    file = sched_type + ".csv"
    
    with open(file, "r") as f:
        lines = f.readlines()
    
        timesteps = list(range(0, len(lines) * interval, interval))

        sys_energy   = [float(line.split(",")[0]) for line in lines] 
        proc1_energy = [float(line.split(",")[1]) for line in lines]
        proc2_energy = [float(line.split(",")[3]) for line in lines]

        plt.plot(timesteps, sys_energy, label='sys_energy')
        plt.plot(timesteps, proc1_energy, label='proc1_energy')
        plt.plot(timesteps, proc2_energy, label='proc2_energy')

        plt.xlabel('Timesteps')
        plt.ylabel('Energy')
        plt.legend()

        plt.savefig(sched_type + 'graph.png')

if __name__ == "__main__":
    plot_bench_graph(sys.argv[1], int(sys.argv[2]))