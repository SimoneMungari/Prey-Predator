PREDA PREDATORE
Il programma utilizza librerie esterne : Allegro 5 ed MPI.

Installazione Allegro :
1) sudo add-apt-repository ppa:allegro/5.2
2) sudo apt-get update
3) sudo apt-get install liballegro5-dev
Fonte https://wiki.allegro.cc/index.php?title=Install_Allegro_from_Ubuntu_PPAs

Come compilare ed eseguire il programma :
mpiCC main.cpp -lallegro -lallegro_primitives -o main
mpiexec -n 2 main

Osservazione
Se si vuole cambiare il numero di processi (2) basta cambiare la costante "numThread" all'interno del programma e scrivere il numero di processi desiderato al posto del "2".


PREY PREDATOR
The program uses external libraries : Allegro 5 and MPI.

Install Allegro :
1) sudo add-apt-repository ppa:allegro/5.2
2) sudo apt-get update
3) sudo apt-get install liballegro5-dev
Source https://wiki.allegro.cc/index.php?title=Install_Allegro_from_Ubuntu_PPAs

How to compile and execute the program :
mpiCC main.cpp -lallegro -lallegro_primitives -o main
mpiexec -n 2 main

Observation
If you want to change the number of processes (2) you can change the constant "numThread" inside the program and write the number of processes that you want instead of "2".
