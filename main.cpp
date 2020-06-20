#include <iostream>
#include <mpi.h>
#include <time.h>
#include <cstdlib>
#include <allegro5/allegro.h>
#include "allegro5/allegro_primitives.h"
#include <string>
#include <vector>

using namespace std;


struct Organismo
{
    short contRiproduzione = 0; // contatori per tenere traccia dell'organismo in che condizioni è:
    short contMorte = 0;        // se è il momento di riprodursi o se è il momento di morire
    short xSucc = 0; /* la posizione che assumerà nella prossima mossa */ 
    short ySucc = 0; /* la posizione che assumerà nella prossima mossa */
    short xRiproduzione = -1; // la posizione dove si riprodurrà, se rimane -1 vuol dire che ancora
    short yRiproduzione = -1; // non è tempo di riprodursi
    short direzione = 2; //0 == SU ; 1 == GIU; 2 == NULLO
    //se la direzione è nulla vuol dire che non deve spostarsi nell'altra sottomatrice
    char tipo; // 'P' per predatore di colore nero
               //'p' per le prede di colore blu
               //'n' se è vuoto di colore verde
};

const int numRighe = 100; // righe e
const int numColonne = 100;// colonne che costituiscono la struttura della matrice principale completa

bool muoviOrganismi = false; // tiene traccia in quale step siamo, se false : turno in cui si decidono
                            // le prossime posizioni , se true : verranno effettuati i movimenti

const int numThread = 2;   // numero di processi che vengono "invocati"

const int numRighe2 = (numRighe/numThread); // righe che costituiranno la struttura delle matrici
                                            // di ogni processo

const int STEP_RIPRODUZIONE_PREDE = 16; // numero degli step prima di arrivare alla riproduzione delle prede
const int STEP_RIPRODUZIONE_PREDATORI = 20; // numero degli step prima di arrivare alla riproduzione dei predatori
const int STEP_MORTE = 15;        // numero degli step prima di morire

const int TURNI = 10000;

int id; // identificativo del singolo thread

MPI_Datatype type_organismo; // tipologia derivata di MPI basata sulla struct Organismo

Organismo worldInit[numRighe][numColonne]; // matrice "supervisore" che contiene tutte le sottomatrici
                                           // viene modificata solo dal thread master (0)

Organismo world[numRighe2][numColonne];    // sottomatrice di worldInit, processata dal thread che la possiede 
Organismo worldComodo[numRighe2][numColonne]; // sottomatrice di "scambio" con world
Organismo ghostCells[2][numColonne]; // righe "ghost" o "halo"

void createDataType(MPI_Datatype &datatype);
void popolaWorld();
void invioGhostCells();
void ricezioneGhostCells();

void game();
void copiaMondo();

void movimento(int i, int j);
void movimentoPredatore(int i, int j, vector<pair<int,int> > prede, vector<pair<int,int> > posti);
void movimentoPreda_Riproduzione(int i, int j, vector<pair<int,int> > posti, char organismo, bool riproduzione);
vector<pair<int, int> > cercaDintorni(int i, int j, char organismo);
void distruggiOrganismo(int i, int j);

void stampaMondo();

int main(int arg, char** argvs)
{
    srand(time(NULL));

    //Inizializzazioni delle funzioni e delle variabili di allegro
    if(!al_init())
    {
        cout<<"Errore init allegro"<<endl;
        return 0;
    }
    if(!al_init_primitives_addon())
    {
        cout<<"failed image addon";
        return 0;
    }
    ALLEGRO_DISPLAY *display;
    ALLEGRO_EVENT_QUEUE *event_queue;
    ALLEGRO_TIMER *timer;
    ALLEGRO_EVENT ev;
    event_queue = al_create_event_queue();
    timer = al_create_timer(1.0 / 60);

    //Inizializzazioni delle funzioni e variabili MPI con annessa creazione della tipologia organismo
    MPI_Init(&arg, &argvs);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);

    createDataType(type_organismo);

    for(int i = 0; i < numRighe2; i++)
        for(int j = 0; j < numColonne; j++)
        {
            worldComodo[i][j].tipo = 'n';       //inizializzazione matrice di supporto
        }

    if(id == 0) //il thread master crea il display e invoca la funzione per riempire la matrice worldInit
    {
        display = al_create_display(400,400);
        popolaWorld();
    }

    // suddivisione della matrice principale nelle sottomatrici "world" tramite funzione MPI Scatter
    // e scambio delle righe ghost
    MPI_Scatter(worldInit, numRighe2*numColonne, type_organismo, world, numRighe2*numColonne, type_organismo, 0, MPI_COMM_WORLD); 
    invioGhostCells();
    ricezioneGhostCells();

    int contTurno = 0; // contatore per tenere traccia dei turni

    //funzioni di ALLEGRO per la gestione della stampa della matrice tramite timer
    al_register_event_source(event_queue, al_get_timer_event_source(timer));
    al_start_timer(timer);

    while(contTurno < TURNI)
    {
        al_wait_for_event(event_queue, &ev); //rimane in attesa di un evento
        if(ev.type == ALLEGRO_EVENT_TIMER)  //se l'evento è il timer allora può partire il "turno"
        {
            MPI_Barrier(MPI_COMM_WORLD);    //barrier per far iniziare tutti i thread nello stesso momento
            game(); //inizio del gioco

            if(!muoviOrganismi) //step in cui sono stati fatti i movimenti degli organismi
            {
                copiaMondo();  // funzione per fare lo swap tra la matrice di supporto e la matrice world

                //le singole sottomatrici vengono spedite alla matrice principale worldInit per la stampa
                MPI_Gather(world, numRighe2*numColonne, type_organismo, worldInit, numRighe2*numColonne, type_organismo, 0, MPI_COMM_WORLD);
                contTurno++;
                if(id == 0)
                  stampaMondo(); //il thread master invoca la funzione di stampa
                
            }
            else //step in cui ancora non sono stati fatti i movimenti, sono solo stati decisi,
            {   //, vengono inviate le righe ghost

                invioGhostCells();
                ricezioneGhostCells();
            }
        }
    }

    //funzioni finali per il deallocamento delle variabili di ALLEGRO e per "chiudere" le funzioni di MPI
    MPI_Finalize();
    if(id == 0)
        al_destroy_display(display);
    al_destroy_event_queue(event_queue);
    al_destroy_timer(timer);
    
}

/* funzione per creare il tipo derivato organismo */
void createDataType(MPI_Datatype &datatype)
{
    int blockLenghtsOrganismo[8] = {1,1,1,1,1,1,1,1};
    MPI_Aint offsetOrganismo[8];
    offsetOrganismo[0] = offsetof(Organismo, contRiproduzione); //funzione che ritorna l'offset
    offsetOrganismo[1] = offsetof(Organismo, contMorte);        //in sostituzione all'extent
    offsetOrganismo[2] = offsetof(Organismo, xSucc);
    offsetOrganismo[3] = offsetof(Organismo, ySucc);
    offsetOrganismo[4] = offsetof(Organismo, xRiproduzione);
    offsetOrganismo[5] = offsetof(Organismo, yRiproduzione);
    offsetOrganismo[6] = offsetof(Organismo, direzione);
    offsetOrganismo[7] = offsetof(Organismo, tipo);
    MPI_Datatype typeOrganismo[8] = {MPI_SHORT, MPI_SHORT, MPI_SHORT, MPI_SHORT, MPI_SHORT, MPI_SHORT, MPI_SHORT, MPI_CHAR};
    MPI_Type_struct(8, blockLenghtsOrganismo, offsetOrganismo, typeOrganismo, &datatype);
    MPI_Type_commit(&datatype);
}

/* funzione per popolare la matrice principale */
void popolaWorld()
{
    for(int i = 0; i < numRighe; i++)
    {
        for(int j = 0; j < numColonne; j++)
        {
            if(rand()%10 == 0)
            {
               worldInit[i][j].tipo = 'p';
            }
            else if(rand()%30 == 0)
            {
                worldInit[i][j].tipo = 'P';
            }
            else
            {
                worldInit[i][j].tipo = 'n';
            }
        }
    }
}

/* funzione per inviare la propria prima e ultima riga agli altri processi che diventeranno ghost,
le comunicazioni sono fatte in modo non bloccante*/
void invioGhostCells()
{
    if(numThread == 1)
        return;
    MPI_Request request;
    if(id == 0) //il processo 0 invia soltanto la riga di sotto
    {
        MPI_Isend(world[numRighe2-1], numColonne, type_organismo, id+1, 99, MPI_COMM_WORLD, &request);             
    }
    else if(id == numThread-1)  //l'ultimo processo invia soltanto la sua prima riga
    {
        MPI_Isend(world[0], numColonne, type_organismo, id-1, 99, MPI_COMM_WORLD, &request);      
    }
    else    // gli altri processi inviano prima e ultima riga
    {
        MPI_Isend(world[0], numColonne, type_organismo, id-1, 99, MPI_COMM_WORLD, &request);
        MPI_Isend(world[numRighe2-1], numColonne, type_organismo, id+1, 99, MPI_COMM_WORLD, &request);        
    }

}

/*funzione per ricevere prima e ultima riga dei processi "vicini" e inserirle come ghost cell, 
le comunicazioni sono non bloccanti*/
void ricezioneGhostCells()
{
    if(numThread == 1)
        return;
    MPI_Request request;
    if(id == 0)
    {
        MPI_Irecv(ghostCells[1], numColonne, type_organismo, id+1, 99, MPI_COMM_WORLD, &request);
    }
    else if(id == numThread-1)
    {
        MPI_Irecv(ghostCells[0], numColonne, type_organismo, id-1, 99, MPI_COMM_WORLD, &request);
    }
    else
    {
        MPI_Irecv(ghostCells[0], numColonne, type_organismo, id-1, 99, MPI_COMM_WORLD, &request);
        MPI_Irecv(ghostCells[1], numColonne, type_organismo, id+1, 99, MPI_COMM_WORLD, &request);
    }

}

/* funzione core del programma, divisa in due step */
void game()
{
    if(muoviOrganismi) //step per muovere gli organismi
    {
        muoviOrganismi = false;
        
        for(int k = 0; k < 2; k++) //priorità alle celle ghost
        {
            for(int i = 0; i < numColonne; i++)
            {
                //se qualche organismo nella cella ghost superiore vuole muoversi verso sotto o
                //se qualche organismo nella cella ghost inferiore vuole muoversi verso sopra
                if((ghostCells[k][i].direzione == 1 && k == 0 && id != 0) 
                || (ghostCells[k][i].direzione == 0 && k == 1 && id != numThread-1))
                {
                    if(ghostCells[k][i].contMorte > STEP_MORTE) //se l'organismo arriva alla morte salta il ciclo
                    {
                        continue;
                    }
                    int xSucc = ghostCells[k][i].xSucc;
                    int ySucc = ghostCells[k][i].ySucc;
                    int xRiproduzione = ghostCells[k][i].xRiproduzione;
                    int yRiproduzione = ghostCells[k][i].yRiproduzione;

                    // se sono entrambi diversi da -1 vuol dire che deve riprodursi
                    // nelle posizioni precedentemente specificate
                    if(xRiproduzione != -1 && yRiproduzione != -1) 
                    {   
                        worldComodo[xRiproduzione][yRiproduzione] = ghostCells[k][i];
                        worldComodo[xRiproduzione][yRiproduzione].contRiproduzione = 0;
                    }
                    else // se non è il turno di riproduzione l'organismo si sposta nella cella prevista
                    {
                        if(world[xSucc][ySucc].tipo == 'p' && ghostCells[k][i].tipo == 'P') //se nella cella prevista c'è una preda, viene mangiata
                        {
                            distruggiOrganismo(xSucc,ySucc);
                            ghostCells[k][i].contMorte = 0; // il predatore ora è sazio
                        }
                        worldComodo[xSucc][ySucc] = ghostCells[k][i]; 
                    }
                }
            }
        }

        // il secondo movimento viene effettuato dai predatori
         for(int i = 0; i < numRighe2; i++)
        {   
            for(int j = 0; j < numColonne; j++)
            {
                if(world[i][j].tipo == 'P' &&
                (world[i][j].direzione == 2 || (world[i][j].direzione != 2 && world[i][j].xRiproduzione != -1))) 
                {
                    if(world[i][j].contMorte > STEP_MORTE)
                    {
                        distruggiOrganismo(i, j);
                        continue;
                    }
                    int xSucc = world[i][j].xSucc;
                    int ySucc = world[i][j].ySucc;
                    int xRiproduzione = world[i][j].xRiproduzione;
                    int yRiproduzione = world[i][j].yRiproduzione;
                    if(xRiproduzione != -1 && yRiproduzione != -1)
                    {
                        if(worldComodo[xRiproduzione][yRiproduzione].tipo == 'n')
                        {
                            worldComodo[xRiproduzione][yRiproduzione] = world[i][j];
                            worldComodo[xRiproduzione][yRiproduzione].contRiproduzione = 0;
                        }
                        if(worldComodo[xSucc][ySucc].tipo == 'n')
                        {
                            worldComodo[xSucc][ySucc] = world[i][j];
                            worldComodo[xSucc][ySucc].contRiproduzione = 0;
                        }
                    }
                    else
                    {
                        if(worldComodo[xSucc][ySucc].tipo == 'n')
                        {
                            if(world[xSucc][ySucc].tipo == 'p')
                            {
                                distruggiOrganismo(xSucc,ySucc);
                                world[i][j].contMorte = 0;
                            }
                            worldComodo[xSucc][ySucc] = world[i][j];
                        }   
                        
                    }
                    
                }
            }
        }

        //ultimo movimento è delle prede
        for(int i = 0; i < numRighe2; i++)
        {   
            for(int j = 0; j < numColonne; j++)
            {
                if(world[i][j].tipo == 'p' &&
                (world[i][j].direzione == 2 || (world[i][j].direzione != 2 && world[i][j].xRiproduzione != -1))) 
                {
                    int xSucc = world[i][j].xSucc;
                    int ySucc = world[i][j].ySucc;
                    int xRiproduzione = world[i][j].xRiproduzione;
                    int yRiproduzione = world[i][j].yRiproduzione;
                    if(xRiproduzione != -1 && yRiproduzione != -1)
                    {
                        if(worldComodo[xRiproduzione][yRiproduzione].tipo == 'n')
                        {
                            worldComodo[xRiproduzione][yRiproduzione] = world[i][j];
                            worldComodo[xRiproduzione][yRiproduzione].contRiproduzione = 0;
                        }
                        if(worldComodo[xSucc][ySucc].tipo == 'n')
                        {
                            worldComodo[xSucc][ySucc] = world[i][j];
                            worldComodo[xSucc][ySucc].contRiproduzione = 0;
                        }
                    }
                    else if(worldComodo[xSucc][ySucc].tipo == 'n')
                        worldComodo[xSucc][ySucc] = world[i][j];
                    
                }
            }
        }
    }
    else //step per decidere le future posizioni
    {
        for(int i = 0; i < numRighe2; i++) //viene svuotata la matrice di supporto
            for(int j = 0; j < numColonne; j++)
            {
                worldComodo[i][j].tipo = 'n';
                worldComodo[i][j].contMorte = 0;
                worldComodo[i][j].contRiproduzione = 0;
            }
        for(int i = 0; i < numRighe2; i++) //priorità ai predatori per decidere la posizione
        {   
            for(int j = 0; j < numColonne; j++)
            {
                if(world[i][j].tipo == 'P')
                {
                    movimento(i , j); //per i predatori    
                }
            }
        }

        for(int i = 0; i < numRighe2; i++)
        {         
            
            for(int j = 0; j < numColonne; j++)
            {
                if(world[i][j].tipo == 'p')
                {
                    movimento(i , j); //per le prede
                }
            }
            
        }

        muoviOrganismi = true;
        for(int i = 0; i < numRighe2; i++)
            for(int j = 0; j < numColonne; j++)
            {
                worldComodo[i][j].tipo = 'n';
                worldComodo[i][j].contMorte = 0;
                worldComodo[i][j].contRiproduzione = 0;
            }
    }
    
}

/* funzione per lo swap della sottomatrice di supporto con la sottomatrice world*/
void copiaMondo()
{
    for(int i = 0; i < numRighe2; i++)
    {
        for(int j = 0; j < numColonne; j++)
        {
            Organismo temp = worldComodo[i][j];
            worldComodo[i][j] = world[i][j];
            world[i][j] = temp;
        }
    }
}

/*funzione per decidere quale sarà la prossima posizione */
void movimento(int i, int j)
{
    //incremento degli stati "Morte" e "Riproduzione"

    if(world[i][j].tipo == 'P') //solo i predatori possono morire
        world[i][j].contMorte++;
    world[i][j].contRiproduzione++;

    
    vector<pair<int, int> > prede;  //riempita con le coordinate delle prede adiacenti
    vector<pair<int, int> > posti; //riempita con le coordinate dei posti vuoti adiacenti

    if((world[i][j].tipo == 'P' && world[i][j].contRiproduzione > STEP_RIPRODUZIONE_PREDATORI) ||
    (world[i][j].tipo == 'p' && world[i][j].contRiproduzione > STEP_RIPRODUZIONE_PREDE)) //se è il turno di riprodursi
        {
            posti = cercaDintorni(i, j, 'n');
            movimentoPreda_Riproduzione(i, j, posti, world[i][j].tipo, true);
            return;
        }


    //se non è il turno di riprodursi vengono settate le coordinate della riproduzione a -1
    world[i][j].xRiproduzione = -1;
    world[i][j].yRiproduzione = -1;

    //vengono riempiti i due vettori con le coordinate delle prede e dei posti liberi adiacenti
    if(world[i][j].tipo == 'P')
        prede = cercaDintorni(i, j, 'p'); //il vettore delle prede viene riempito solo se stiamo prevedendo il movimento di un predatore
    posti = cercaDintorni(i, j, 'n');


    if(world[i][j].tipo == 'P')
        movimentoPredatore(i,j,prede,posti);
    else if(world[i][j].tipo == 'p')
        movimentoPreda_Riproduzione(i,j,posti, 'p', false);
    
}

/* serie di if per capire quali organismi (in base al char che viene inviato) ci sono nelle celle adiacenti */
vector<pair<int, int> > cercaDintorni(int i, int j, char organismo)
{
    vector<pair<int, int> > dintorni;
    if(i+1 < numRighe2 && world[i+1][j].tipo == organismo)
        dintorni.push_back({i+1,j});

    if(j+1 < numColonne && world[i][j+1].tipo == organismo)
        dintorni.push_back({i,j+1});

    if(i+1 < numRighe2 && j+1 < numColonne && world[i+1][j+1].tipo == organismo)
        dintorni.push_back({i+1,j+1});

    if(i-1 >= 0 && world[i-1][j].tipo == organismo)
        dintorni.push_back({i-1,j});

    if(j-1 >= 0 && world[i][j-1].tipo == organismo)
        dintorni.push_back({i,j-1});

    if(i-1 >= 0 && j-1 >= 0 && world[i-1][j-1].tipo == organismo) 
        dintorni.push_back({i-1,j-1});

    if(i-1 >= 0 && j+1 < numColonne && world[i-1][j+1].tipo == organismo)
        dintorni.push_back({i-1,j+1});
    
    if(i+1 < numRighe2 && j-1 >= 0  && world[i+1][j-1].tipo == organismo)
        dintorni.push_back({i+1,j-1});
    
    if(numThread > 1)
    {
        if(i+1 >= numRighe2 && id != numThread-1 && ghostCells[1][j].tipo == organismo)
        dintorni.push_back({i+1,j});
    
        if(i-1 < 0 && id != 0 && ghostCells[0][j].tipo == organismo)
            dintorni.push_back({i-1,j});
        
        if(i-1 < 0 && j-1 >= 0 && id != 0 && ghostCells[0][j-1].tipo == organismo) 
            dintorni.push_back({i-1,j-1});

        if(i-1 < 0 && j+1 < numColonne && id != 0 && ghostCells[0][j+1].tipo == organismo)
            dintorni.push_back({i-1,j+1});
        
        if(i+1 >= numRighe2 && j-1 >= 0 && id != numThread-1 && ghostCells[1][j-1].tipo == organismo)
            dintorni.push_back({i+1,j-1});
        
        if(i+1 >= numRighe2 && j+1 < numColonne && id != numThread-1 && ghostCells[1][j+1].tipo == organismo)
            dintorni.push_back({i+1,j+1});
    }

    return dintorni;

}

/*funzione per decidere il movimento di un predatore, controlla anche che nella futura posizione
non ci sia già qualcun altro, in caso contrario cerca un'altra posizione fra quelle disponibili
entro un tot cicli*/
void movimentoPredatore(int i, int j, vector<pair<int,int> > prede, vector<pair<int,int> > posti)
{
    int x = -1;
    int y = -1;
    int cont = 0;
    bool trovato = false; //indica se è stata trovata una posizione
    bool ghost0 = false; //indica se la posizione trovata fa parte della ghost cell
    bool ghost1 = false;
    bool mangiato = false;  //indica se spostandosi nella futura cella mangerà una preda

    if(prede.size() > 0) //se ci sono delle prede nei dintorni il predatore cerca una di loro
    {
        while(true)
        {
            if(cont >= prede.size()*5) // se il contatore è arrivato a tot cicli e ancora non ha trovato
            {                           // una posizione dove andare, rimane fermo
                break;
            }
            int pos = rand() % prede.size(); // futura posizione in modo random
            x = prede[pos].first;
            y = prede[pos].second;
            cont++;
            if(x >= numRighe2 && id != numThread-1) //se la posizione è nella ghost cell inferiore
            {
                if(ghostCells[1][y].tipo == 'n')
                {
                    ghost1 = true;
                    trovato = true;
                    mangiato = true;
                    break;
                }
            }
            else if(x < 0 && id != 0) //se la posizione è nella ghost cell superiore
            {
                if(ghostCells[0][y].tipo == 'n')
                {
                    ghost0 = true;
                    trovato = true;
                    mangiato = true;
                    break;
                }
            }
            else if(worldComodo[x][y].tipo == 'n') //se la posizione fa parte della sua sottomatrice
            {
                ghost0 = false;
                ghost1 = false;
                trovato = true;
                mangiato = true;
                break;
            }
        }

        
    }
    else if(posti.size() > 0) //se non ci sono prede nei dintorni ma ci sono dei posti liberi
    {
        while(true)
        {
            if(cont >= posti.size()*5)
            {
                break;
            }
            int pos = rand() % posti.size();
            x = posti[pos].first;
            y = posti[pos].second;
            cont++;
            if(x >= numRighe2 && id != numThread-1)
            {
                if(ghostCells[1][y].tipo == 'n')
                {
                    ghost1 = true;
                    trovato = true;
                    x = 1;
                    break;
                }
            }
            else if(x < 0 && id != 0)
            {
                if(ghostCells[0][y].tipo == 'n')
                {
                    ghost0 = true;
                    trovato = true;
                    x = 0;
                    break;
                }
            }
            else if(worldComodo[x][y].tipo == 'n')
            {
                ghost0 = false;
                ghost1 = false;
                trovato = true;
                break;
            }
        }

    }


    if(trovato) //se ha trovato una nuova posizione
    {
        world[i][j].xSucc = x;
        world[i][j].ySucc = y;
        world[i][j].direzione = 2;
        if(ghost0)
        {
            ghostCells[0][y] = world[i][j];
            world[i][j].direzione = 0;
            world[i][j].xSucc = numRighe2-1;
        }
        else if(ghost1)
        {
            ghostCells[1][y] = world[i][j];
            world[i][j].direzione = 1;
            world[i][j].xSucc = 0;
        }
        else
            worldComodo[x][y] = world[i][j];
    }
    else //in caso contrario rimarrà nella stessa posizione
    {
        worldComodo[i][i] = world[i][j];
        world[i][j].xSucc = i;
        world[i][j].ySucc = j;
        world[i][j].direzione = 2;
    }
}

/*funzione per decidere il movimento di una preda o di una riproduzione, controlla anche che nella futura posizione
non ci sia già qualcun altro, in caso contrario cerca un'altra posizione fra quelle disponibili
entro un tot cicli, la posizione dell'organismo rimarrà la stessa, la funzione serve a cercare la posizione
del nuovo organismo che nascerà*/
void movimentoPreda_Riproduzione(int i, int j, vector<pair<int,int> > posti, char organismo, bool riproduzione)
{
    int x = -1;
    int y = -1;
    int cont = 0;
    bool trovato = false;
    bool ghost0 = false;
    bool ghost1 = false;
    if(posti.size() > 0)
    {
        while(true)
        {
            if(cont >= posti.size()*5)
            {
                break;
            }
            int pos = rand() % posti.size();
            x = posti[pos].first;
            y = posti[pos].second;
            cont++;
            if(x >= numRighe2 && id != numThread-1)
            {
                if(ghostCells[1][y].tipo == 'n')
                {
                    ghost1 = true;
                    trovato = true;
                    break;
                }
            }
            else if(x < 0 && id != 0)
            {
                if(ghostCells[0][y].tipo == 'n')
                {
                    ghost0 = true;
                    trovato = true;
                    break;
                }
            }
            else if(worldComodo[x][y].tipo == 'n')
            {
                ghost0 = false;
                ghost1 = false;
                trovato = true;
                break;
            }
        }

    }
    if(trovato)
    {
        if(riproduzione)
        {
            worldComodo[i][i] = world[i][j];
            world[i][j].xSucc = i;
            world[i][j].ySucc = j;
            world[i][j].yRiproduzione = y;
            world[i][j].xRiproduzione = x;
            world[i][j].direzione = 2;
            if(ghost0)
            {
                ghostCells[0][y] = world[i][j];
                world[i][j].direzione = 0;
                world[i][j].xRiproduzione = numRighe2-1;

            }
            else if(ghost1)
            {
                ghostCells[1][y] = world[i][j];
                world[i][j].direzione = 1;
                world[i][j].xRiproduzione = 0;
            }
            else
            {
                worldComodo[x][y] = world[i][j];
            }
            

        }
        else
        {
            world[i][j].xSucc = x;
            world[i][j].ySucc = y;
            world[i][j].direzione = 2;
            if(ghost0)
            {
                ghostCells[0][y] = world[i][j];
                world[i][j].direzione = 0;
                world[i][j].xSucc = numRighe2-1;
            }
            else if(ghost1)
            {
                ghostCells[1][y] = world[i][j];
                world[i][j].direzione = 1;
                world[i][j].xSucc = 0;
            }
            else
                worldComodo[x][y] = world[i][j];
        }
        
    }
    else
    {
        worldComodo[i][i] = world[i][j];
        world[i][j].xSucc = i;
        world[i][j].ySucc = j;
        world[i][j].direzione = 2;
    }
}

/* funzione per eliminare un organismo in una determinata posizione (usata solitamente in caso di morte
di una preda da parte di un predatore) */
void distruggiOrganismo(int i, int j)
{
    world[i][j].tipo='n';
    world[i][j].contMorte = 0;
    world[i][j].contRiproduzione = 0;
}

/* sfruttando le funzioni di ALLEGRO verrà stampata la matrice sul display e il numero di prede/predatori su terminale */
void stampaMondo()
{
    al_clear_to_color(al_map_rgb(0,0,0));
    int size = 4;
    int prede = 0;
    int predatori = 0;
    for(int i = 0; i < numRighe; i++)
    {
        for(int j = 0; j < numColonne; j++)
        {
            if(worldInit[i][j].tipo == 'n')
                al_draw_filled_rectangle(j*size,i*size,(j*size)+size,(i*size)+size, al_map_rgb(0, 255, 0));
            else if(worldInit[i][j].tipo == 'p')
            {
                prede++;
                al_draw_filled_rectangle(j*size,i*size,(j*size)+size,(i*size)+size, al_map_rgb(0, 0, 255));
            }
            else if(worldInit[i][j].tipo == 'P')
            {   
                predatori++;
                al_draw_filled_rectangle(j*size,i*size,(j*size)+size,(i*size)+size, al_map_rgb(0, 0, 0));
            }
        }
    }
    cout<<"PREDE: "<<prede<<endl<<"PREDATORI: "<<predatori<<endl<<endl;
    al_flip_display();
}