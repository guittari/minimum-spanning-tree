#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mpi.h>

//#define priq_purge(q) (q)->n = 1
//#define priq_size(q) ((q)->n - 1)

const int EDGE_MEMBERS = 3;
const int UNSET_CANONICAL_ELEMENT = -1;
const int NO_EDGE = -1;
const int MAXIMUM_RANDOM = 20;

typedef struct {
	int algorithm;
	int columns;
	int help;
	int maze;
	int new;
	int parallel;
	int rows;
	int verbose;
} Handle;

typedef struct Node {
	int to;
	int weight;
	struct Node* next;
} LinkedList;

typedef struct {
	int elements;
	LinkedList** lists;
} AdjacencyList;

typedef struct {
	int elements;
	int* canonicalElements;
	int* rank;
} Set;

typedef struct {
	int edges;
	int vertices;
	int* edgeList;
} WeightedGraph;

typedef struct {
	void * data;
	int pri;
} QueueElement;

typedef struct {
	QueueElement *buf;
	int n, alloc;
} PriorityQueue;

/*
 * first element in array not used to simplify indices
 */
PriorityQueue* newPriorityQueue(int size) {
	if (size < 4) {
		size = 4;
	}

	PriorityQueue* q = malloc(sizeof(PriorityQueue));
	q->buf = malloc(sizeof(QueueElement) * size);
	q->alloc = size;
	q->n = 1;

	return q;
}

void pushPriorityQueue(PriorityQueue* q, void *data, int pri) {
	QueueElement *b;
	int n, m;

	if (q->n >= q->alloc) {
		q->alloc *= 2;
		b = q->buf = realloc(q->buf, sizeof(QueueElement) * q->alloc);
	} else {
		b = q->buf;
	}

	n = q->n++;
	// append at end, then up heap
	while ((m = n / 2) && pri < b[m].pri) {
		b[n] = b[m];
		n = m;
	}
	b[n].data = data;
	b[n].pri = pri;
}

/*
 * remove top item. returns 0 if empty. *pri can be null.
 */
void * popPriorityQueue(PriorityQueue* q, int *pri) {
	void *out;
	if (q->n == 1) {
		return 0;
	}

	QueueElement *b = q->buf;

	out = b[1].data;
	if (pri) {
		*pri = b[1].pri;
	}

	// pull last item to top, then down heap.
	--q->n;

	int n = 1, m;
	while ((m = n * 2) < q->n) {
		if (m + 1 < q->n && b[m].pri > b[m + 1].pri) {
			m++;
		}

		if (b[q->n].pri <= b[m].pri) {
			break;
		}
		b[n] = b[m];
		n = m;
	}

	b[n] = b[q->n];
	if (q->n < q->alloc / 2 && q->n >= 16) {
		q->buf = realloc(q->buf, (q->alloc /= 2) * sizeof(b[0]));
	}

	return out;
}

/*
 * get the top element without removing it from queue
 */
void* getMinPriorityQueue(PriorityQueue* q, int *pri) {
	if (q->n == 1) {
		return 0;
	}
	if (pri) {
		*pri = q->buf[1].pri;
	}
	return q->buf[1].data;
}

/*
 * this is O(n log n), but probably not the best
 */
void combinePriorityQueue(PriorityQueue* q, PriorityQueue* q2) {
	int i;
	QueueElement *e = q2->buf + 1;

	for (i = q2->n - 1; i >= 1; i--, e++) {
		pushPriorityQueue(q, e->data, e->pri);
	}
	(q2)->n = 1;
}

void createMazeFile(const int rows, const int columns,
		const char outputFileName[]);
int createsLoop(const WeightedGraph* graph, int currentedge, Set* set);
void deleteSet(Set* set);
void deleteWeightedGraph(WeightedGraph* graph);
int findSet(Set* set, int vertex);
void getNeighbors(const LinkedList* list, int* neighbors, const int vertex,
		const int step);
int insertNode(LinkedList* list, int to, int weight);
void merge(int* edgeList, int start, int size, int pivot);
void mergeSort(int* edgeList, int start2, int size2);
void mstBoruvka(const WeightedGraph* graph, const WeightedGraph* mst);
void mstKruskal(WeightedGraph* graph, const WeightedGraph* mst);
void mstPrim(const WeightedGraph* graph, const WeightedGraph* mst);
void newAdjacencyList(AdjacencyList* list, const WeightedGraph* graph);
LinkedList* newNode(int to, int weight);
void newSet(Set* set, const int elements);
void newWeightedGraph(WeightedGraph* graph, const int vertices, const int edges);
void printAdjacencyList(const AdjacencyList* list);
void printLinkedList(LinkedList* list);
void printMaze(const WeightedGraph* graph, int rows, int columns);
void printSet(const Set* set);
void printWeightedGraph(const WeightedGraph* graph);
Handle processParameters(int argc, char* argv[]);
void readMazeFile(WeightedGraph* graph, const char inputFileName[]);
void sort(WeightedGraph* graph);
void unionSet(Set* set, const int parent1, const int parent2);

/*
 * main program
 */
int main(int argc, char* argv[]) {
	// MPI variables and initialization
	int size;
	int rank;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Datatype MPI_HANDLE;
	MPI_Type_contiguous(sizeof(Handle) / sizeof(int), MPI_INT, &MPI_HANDLE);
	MPI_Type_commit(&MPI_HANDLE);

	// control variable
	Handle handle;

	// graph Variables
	WeightedGraph* graph = &(WeightedGraph ) { .edges = 0, .vertices = 0,
					.edgeList = NULL };
	WeightedGraph* mst = &(WeightedGraph ) { .edges = 0, .vertices = 0,
					.edgeList = NULL };

	if (rank == 0) {
		printf("Starting\n");

		// process command line parameters
		handle = processParameters(argc, argv);
	}

	MPI_Bcast(&handle, 1, MPI_HANDLE, 0, MPI_COMM_WORLD);
	if (handle.help == 1) {
		MPI_Finalize();
		exit(0);
	}

	if (rank == 0) {
		if (handle.new == 1) {
			// create a new maze file
			createMazeFile(handle.rows, handle.columns, "maze.csv");
		}

		// read the maze file and store it in the graph
		readMazeFile(graph, "maze.csv");

		if (handle.verbose == 1) {
			// print the edges of the read graph
			printf("Graph:\n");
			printWeightedGraph(graph);
		}

		newWeightedGraph(mst, graph->vertices, graph->vertices - 1);
	}

	double start = MPI_Wtime();
	if (handle.algorithm == 0) {
		// use Kruskal's algorithm
		mstKruskal(graph, mst);
	} else if (handle.algorithm == 1) {
		// use Prim's algorithm
		mstPrim(graph, mst);
	} else if (handle.algorithm == 2) {
		// use Boruvka's algorithm
		mstBoruvka(graph, mst);
	}

	if (rank == 0) {
		printf("Time elapsed: %f s\n", MPI_Wtime() - start);

		if (handle.verbose == 1) {
			// print the edges of the MST
			printf("MST:\n");
			printWeightedGraph(mst);
		}

		if (handle.maze == 1) {
			// print the maze to the console
			printf("Maze:\n");
			printMaze(mst, handle.rows, handle.columns);
		}

		// cleanup
		deleteWeightedGraph(graph);
		deleteWeightedGraph(mst);

		printf("Finished\n");
	}

	MPI_Finalize();

	exit(0);
}

/*
 * save a 2D (rows x columns) grid graph with random edge weights to a file
 */
void createMazeFile(const int rows, const int columns,
		const char outputFileName[]) {
	// open the file
	FILE* outputFile;
	const char* outputMode = "w";
	outputFile = fopen(outputFileName, outputMode);
	if (outputFile == NULL) {
		printf("Could not open output file, exiting!\n");
		exit(1);
	}

	// first line contains number of vertices and edges
	const int vertices = rows * columns;
	const int edges = vertices * 2 - rows - columns;
	fprintf(outputFile, "%d %d\n", vertices, edges);

	// all lines after the first contain the edges, values stored as "from to weight"
	srand(time(NULL));
	int vertex;
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns; j++) {
			vertex = i * columns + j;
			if (j != columns - 1) {
				fprintf(outputFile, "%d %d %d\n", vertex, vertex + 1,
						rand() % MAXIMUM_RANDOM);
			}
			if (i != rows - 1) {
				fprintf(outputFile, "%d %d %d\n", vertex, vertex + columns,
						rand() % MAXIMUM_RANDOM);
			}
		}
	}

	fclose(outputFile);
}

/*
 * check if adding the edge to the MST would create a loop
 */
int createsLoop(const WeightedGraph* graph, int currentEdge, Set* set) {
	int from = graph->edgeList[currentEdge * EDGE_MEMBERS];
	int to = graph->edgeList[currentEdge * EDGE_MEMBERS + 1];
	if (set->canonicalElements[from] == set->canonicalElements[to]) {
		// adding the edge would create a loop
		return 1;
	} else {
		// adding the edge wouldn't create a loop, update canonical elements
		int replace = set->canonicalElements[from];
		for (int i = 0; i < graph->vertices; i++) {
			if (set->canonicalElements[i] == replace) {
				set->canonicalElements[i] = set->canonicalElements[to];
			}
		}
		return 0;
	}
}

/*
 * cleanup set data
 */
void deleteSet(Set* set) {
	free(set->canonicalElements);
	free(set->rank);
}

/*
 * cleanup graph data
 */
void deleteWeightedGraph(WeightedGraph* graph) {
	free(graph->edgeList);
}

/*
 * return the canonical element of a vertex with path compression
 */
int findSet(Set* set, int vertex) {
	if (set->canonicalElements[vertex] == UNSET_CANONICAL_ELEMENT) {
		return vertex;
	} else {
		set->canonicalElements[vertex] = findSet(set,
				set->canonicalElements[vertex]);
		return set->canonicalElements[vertex];
	}
}

/*
 * get neighbors to specified vertex
 */
void getNeighbors(const LinkedList* list, int* neighbors, const int vertex,
		const int step) {
	if (list->next != NULL) {
		getNeighbors(list->next, neighbors, vertex, step + 2);
	}
	neighbors[step] = list->to;
	neighbors[step + 1] = list->weight;
}

/*
 * insert node at the end, if not existent
 */
int insertNode(LinkedList* list, int to, int weight) {
	int inserted = 0;
	if (list->to != to) {
		if (list->next == NULL) {
			LinkedList* new = newNode(to, weight);
			list->next = new;
			inserted = 1;
		} else {
			inserted = insertNode(list->next, to, weight);
		}
	}
	return inserted;
}

/*
 * merge sorted lists
 */
void merge(int* edgeList, int start, int size, int pivot) {
//	int length = size - start + 1;
//	int* working = (int*) malloc(length * EDGE_MEMBERS * sizeof(int));
//	memset(working, 0, length * EDGE_MEMBERS * sizeof(int));
//	for (int i = 0; i <= pivot; i++) {
//		for (int j = 0; j < EDGE_MEMBERS; j++) {
//			working[i * EDGE_MEMBERS + j] = edgeList[(i + start) * EDGE_MEMBERS
//					+ j];
//		}
//	}
//	for (int i = pivot + 1; i < length; i++) {
//		for (int j = 0; j < EDGE_MEMBERS; j++) {
//			working[(length - i) * EDGE_MEMBERS + j] = edgeList[(i + start)
//					* EDGE_MEMBERS + j];
//		}
//	}

//	for (i = pivot + 1; i > start; i--) {
//		working[i - 1] = edgeList[i - 1];
//	}
//	for (j = pivot; j < size; j++) {
//		working[size + pivot - j] = edgeList[j + 1];
//	}

//	int i = 0;
//	int j = size;
//	for (int k = start; k <= size; k++) {
//		if (working[j * EDGE_MEMBERS + 2] < working[i * EDGE_MEMBERS + 2]) {
//			for (int l = 0; l < EDGE_MEMBERS; l++) {
//				edgeList[k * EDGE_MEMBERS + l] = working[i * EDGE_MEMBERS + l];
//			}
//			j--;
//		} else {
//			for (int l = 0; l < EDGE_MEMBERS; l++) {
//				edgeList[k * EDGE_MEMBERS + l] = working[i * EDGE_MEMBERS + l];
//			}
//			i++;
//		}
//	}
//	free(working);

	int length = size - start + 1;
	// make a temporary copy of the list for merging
	int* working = (int*) malloc(length * EDGE_MEMBERS * sizeof(int));
	for (int i = 0; i < length; i++) {
		for (int j = 0; j < EDGE_MEMBERS; j++) {
			working[i * EDGE_MEMBERS + j] = edgeList[(i + start) * EDGE_MEMBERS
					+ j];
		}
	}

	// merge the two parts together
	int merge1 = 0;
	int merge2 = pivot - start + 1;
	for (int i = 0; i < length; i++) {
		if (merge2 <= size - start) {
			if (merge1 <= pivot - start) {
				if (working[merge1 * EDGE_MEMBERS + 2]
						> working[merge2 * EDGE_MEMBERS + 2]) {
					for (int j = 0; j < EDGE_MEMBERS; j++) {
						edgeList[(i + start) * EDGE_MEMBERS + j] =
								working[merge2 * EDGE_MEMBERS + j];
					}
					merge2++;
				} else {
					for (int j = 0; j < EDGE_MEMBERS; j++) {
						edgeList[(i + start) * EDGE_MEMBERS + j] =
								working[merge1 * EDGE_MEMBERS + j];
					}
					merge1++;
				}
			} else {
				for (int j = 0; j < EDGE_MEMBERS; j++) {
					edgeList[(i + start) * EDGE_MEMBERS + j] = working[merge2
							* EDGE_MEMBERS + j];
				}
				merge2++;
			}
		} else {
			for (int j = 0; j < EDGE_MEMBERS; j++) {
				edgeList[(i + start) * EDGE_MEMBERS + j] = working[merge1
						* EDGE_MEMBERS + j];
			}
			merge1++;
		}
	}

	free(working);
}

/*
 * sort the edge list using merge sort
 */
void mergeSort(int* edgeList, int start, int size) {
	if (start == size) {
		// already sorted
		return;
	}

	// recursively divide the list in two parts and sort them
	int pivot = (start + size) / 2;
	mergeSort(edgeList, start, pivot);
	mergeSort(edgeList, pivot + 1, size);

	merge(edgeList, start, size, pivot);
}

/*
 * TODO
 *
 * find a MST of the graph using Boruvka's algorithm
 */
void mstBoruvka(const WeightedGraph* graph, const WeightedGraph* mst) {
	printf("DUMMY!\n");

	// create needed data structures
	Set* set = &(Set ) { .elements = 0, .canonicalElements = NULL, .rank =
			NULL };
	AdjacencyList* list = &(AdjacencyList ) { .elements = 0, .lists = NULL };
	newSet(set, graph->vertices);
	newAdjacencyList(list, graph);
	int* neighbors[list->elements];
	for (int i = 0; i < graph->vertices; i++) {
		neighbors[i] = (int*) malloc(2 * list->lists[i]->to * sizeof(int));
		getNeighbors(list->lists[i]->next, neighbors[i], i, 0);
	}

	// clean up
	deleteSet(set);
	for (int i = 0; i < list->elements; i++) {
		free(neighbors[i]);
	}

	/*		// foreach tree in forest, find closest edge
	 // if edge weights are equal, ties are broken in favor of first edge
	 // in G.Edge()
	 Edge[] closest = new Edge[G.V()];
	 for (Edge e : G.Edge())
	 {
	 int v = e.either(), w = e.other(v);
	 int i = uf.find(v), j = uf.find(w);
	 if (i == j)
	 continue;   // same tree
	 if (closest[i] == null || less(e, closest[i]))
	 closest[i] = e;
	 if (closest[j] == null || less(e, closest[j]))
	 closest[j] = e;
	 }

	 // add newly discovered Edge to MST
	 for (int i = 0; i < G.V(); i++) {
	 Edge e = closest[i];
	 if (e != null) {
	 int v = e.either(), w = e.other(v);
	 // don't add the same edge twice
	 if (!uf.connected(v, w)) {
	 mst.add(e);
	 weight += e.weight();
	 uf.union(v, w);
	 }
	 }*/
}

/*
 * find a MST of the graph using Kruskal's algorithm
 */
void mstKruskal(WeightedGraph* graph, const WeightedGraph* mst) {
	// create needed data structures
	Set* set = &(Set ) { .elements = 0, .canonicalElements = NULL, .rank =
			NULL };
	newSet(set, graph->vertices);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	// sort the edges of the graph
	double start = MPI_Wtime();
	sort(graph);

	if (rank == 0) {
		printf("Time for sorting: %f s\n", MPI_Wtime() - start);

		// add edges to the MST
		int currentEdge = 0;
		int edgesMST = 0;
		while (edgesMST < graph->vertices - 1 || currentEdge < graph->edges) {
			// check for loops if edge would be inserted
			int canonicalElementFrom = findSet(set,
					graph->edgeList[currentEdge * EDGE_MEMBERS]);
			int canonicalElementTo = findSet(set,
					graph->edgeList[currentEdge * EDGE_MEMBERS + 1]);
			if (canonicalElementFrom != canonicalElementTo) {
				// add edge to MST
				for (int i = 0; i < EDGE_MEMBERS; i++) {
					mst->edgeList[edgesMST * EDGE_MEMBERS + i] =
							graph->edgeList[currentEdge * EDGE_MEMBERS + i];
				}
				unionSet(set, canonicalElementFrom, canonicalElementTo);
				edgesMST++;
			}
			currentEdge++;
		}
	}

	// clean up
	deleteSet(set);
}

/*
 * TODO
 *
 * find a MST of the graph using Prim's algorithm
 *
 G: Graph
 VG: Knotenmenge von G
 w: Gewichtsfunktion für Kantenlänge
 r: Startknoten (r ∈ VG)
 Q: Prioritätswarteschlange
 π[u]: Elternknoten von Knoten u im Spannbaum
 Adj[u]: Adjazenzliste von u (alle Nachbarknoten)
 wert[u]: Abstand von u zum entstehenden Spannbaum

 algorithmus_von_prim(G,w,r)
 01  Q \leftarrow VG   //Initialisierung
 02  für alle u ∈ Q
 03      wert[u] \leftarrow ∞
 04      π[u] \leftarrow 0
 05  wert[r] \leftarrow 0
 06  solange Q ≠ \varnothing
 07      u \leftarrow extract_min(Q)
 08      für alle v ∈ Adj[u]
 09          wenn v ∈ Q und w(u,v) < wert[v]
 10              dann π[v] \leftarrow u
 11                  wert[v] \leftarrow w(u,v)
 */
void mstPrim(const WeightedGraph* graph, const WeightedGraph* mst) {
	printf("DUMMY!\n");

	// create needed data structures
	AdjacencyList* list = &(AdjacencyList ) { .elements = 0, .lists = NULL };
	newAdjacencyList(list, graph);
}

/*
 * create adjacency list
 */
void newAdjacencyList(AdjacencyList* list, const WeightedGraph* graph) {
	list->elements = graph->vertices;
	list->lists = (LinkedList**) malloc(graph->vertices * sizeof(LinkedList));
	int inserts[graph->vertices];
	for (int i = 0; i < graph->vertices; i++) {
		inserts[i] = 0;
	}
	for (int i = 0; i < graph->vertices; i++) {
		list->lists[i] = newNode(i, 0);
	}
	for (int i = 0; i < graph->edges; i++) {
		if (insertNode(list->lists[graph->edgeList[i * EDGE_MEMBERS]],
				graph->edgeList[i * EDGE_MEMBERS + 1],
				graph->edgeList[i * EDGE_MEMBERS + 2])) {
			inserts[graph->edgeList[i * EDGE_MEMBERS]]++;
		}
		if (insertNode(list->lists[graph->edgeList[i * EDGE_MEMBERS + 1]],
				graph->edgeList[i * EDGE_MEMBERS],
				graph->edgeList[i * EDGE_MEMBERS + 2])) {
			inserts[graph->edgeList[i * EDGE_MEMBERS + 1]]++;
		}
	}
	for (int i = 0; i < graph->vertices; i++) {
		list->lists[i]->to = inserts[i];
	}
}

/*
 * create new node
 */
LinkedList* newNode(int to, int weight) {
	LinkedList* node = malloc(sizeof(LinkedList));
	node->to = to;
	node->weight = weight;
	node->next = NULL;
	return node;
}

/*
 * initialize and allocate memory for the members of the graph
 */
void newSet(Set* set, const int elements) {
	set->elements = elements;
	set->canonicalElements = (int*) malloc(elements * sizeof(int));
	memset(set->canonicalElements, UNSET_CANONICAL_ELEMENT,
			elements * sizeof(int));
	set->rank = (int*) malloc(elements * sizeof(int));
	memset(set->rank, 0, elements * sizeof(int));
}

/*
 * initialize and allocate memory for the members of the graph
 */
void newWeightedGraph(WeightedGraph* graph, const int vertices, const int edges) {
	graph->edges = edges;
	graph->vertices = vertices;
	graph->edgeList = (int*) malloc(edges * EDGE_MEMBERS * sizeof(int));
	memset(graph->edgeList, 0, edges * EDGE_MEMBERS * sizeof(int));
}

/*
 * prints the adjacency list
 */
void printAdjacencyList(const AdjacencyList* list) {
	for (int i = 0; i < list->elements; i++) {
		printLinkedList(list->lists[i]);
		printf("\n");
	}
}

/*
 * print the data of a linked list
 */
void printLinkedList(LinkedList* list) {
	printf("%d(%d) ", list->to, list->weight);
	if (list->next != NULL) {
		printLinkedList(list->next);
	}
}

/*
 * print the graph as a maze to console
 */
void printMaze(const WeightedGraph* graph, int rows, int columns) {
	// initialize the maze with spaces
	int rowsMaze = rows * 2 - 1;
	int columnsMaze = columns * 2 - 1;
	char maze[rowsMaze][columnsMaze];
	memset(maze, ' ', rowsMaze * columnsMaze * sizeof(char));

	// each vertex is represented as a plus sign
	for (int i = 0; i < rowsMaze; i++) {
		for (int j = 0; j < columnsMaze; j++) {
			if (i % 2 == 0 && j % 2 == 0) {
				maze[i][j] = '+';
			}
		}
	}

	// each edge is represented as dash or pipe sign
	for (int i = 0; i < graph->edges; i++) {
		int from = graph->edgeList[i * EDGE_MEMBERS];
		int to = graph->edgeList[i * EDGE_MEMBERS + 1];
		int row = from / columns + to / columns;
		if ((row % 2)) {
			// edges in even rows are displayed as pipes
			maze[row][(to % columns) * 2] = '|';
		} else {
			// edges in uneven rows are displayed as dashes
			maze[row][(to % columns - 1) * 2 + 1] = '-';
		}
	}

	// print the char array to the console
	for (int i = 0; i < rowsMaze; i++) {
		for (int j = 0; j < columnsMaze; j++) {
			printf("%c", maze[i][j]);
		}
		printf("\n");
	}
}

/*
 * print the components of the set
 */
void printSet(const Set* set) {
	for (int i = 0; i < set->elements; i++) {
		printf("%d: %d(%d)\n", i, set->canonicalElements[i], set->rank[i]);
	}
}

/*
 * print all edges of the graph in "from to weight" format
 */
void printWeightedGraph(const WeightedGraph* graph) {
	for (int i = 0; i < graph->edges; i++) {
		for (int j = 0; j < EDGE_MEMBERS; j++) {
			printf("%d\t", graph->edgeList[i * EDGE_MEMBERS + j]);
		}
		printf("\n");
	}
}

/*
 * process the command line parameters and return a Handle struct with them
 */
Handle processParameters(int argc, char* argv[]) {
	Handle handle = { .algorithm = 0, .columns = 3, .maze = 0, .new = 0,
			.parallel = 0, .rows = 2, .verbose = 0 };

	if (argc > 1) {
		while ((argc > 1) && (argv[1][0] == '-')) {
			switch (argv[1][1]) {

			case 'a':
				// choose algorithm
				handle.algorithm = atoi(&argv[2][0]);
				++argv;
				--argc;
				break;

			case 'c':
				// set number of columns
				handle.columns = atoi(&argv[2][0]);
				++argv;
				--argc;
				break;

			case 'h':
				// print help message
				printf(
						"Parameters:\n"
								"\t-a <int>\tchoose algorithm: 0 Kruskal (default), 1 Prim, 2 Boruvka\n"
								"\t-c <int>\tset number of columns (default: 3)\n"
								"\t-h\t\tprint this help message\n"
								"\t-m\t\tprint the resulting maze to console at the end\n"
								"\t-n\t\tcreate a new maze file\n"
								"\t-r <int>\tset number of rows (default: 2)\n"
								"\t-v\t\tprint more information\n"
								"\nThis program is distributed under the terms of the LGPLv3 license\n");
				handle.help = 1;
				break;

			case 'm':
				// print the resulting maze to console at the end
				handle.maze = 1;
				break;

			case 'n':
				// create a new maze file
				handle.new = 1;
				break;

			case 'p':
				// run in parallel
				handle.parallel = 1;
				break;

			case 'r':
				// set number of rows
				handle.rows = atoi(&argv[2][0]);
				++argv;
				--argc;
				break;

			case 'v':
				// print more information
				handle.verbose = 1;
				break;

			default:
				printf("Wrong parameter: %s\n", argv[1]);
				printf("-h for help\n");
				exit(1);
			}

			++argv;
			--argc;
		}
	}

	return handle;
}

/*
 * read a previously generated maze file and store it in the graph
 */
void readMazeFile(WeightedGraph* graph, const char inputFileName[]) {
	// open the file
	FILE* inputFile;
	const char* inputMode = "r";
	inputFile = fopen(inputFileName, inputMode);
	if (inputFile == NULL) {
		printf("Could not open input file, exiting!\n");
		exit(1);
	}

	int fscanfResult;

	// first line contains number of vertices and edges
	int vertices = 0;
	int edges = 0;
	fscanfResult = fscanf(inputFile, "%d %d", &vertices, &edges);
	newWeightedGraph(graph, vertices, edges);

	// all lines after the first contain the edges
	// values stored as "from to weight"
	int from;
	int to;
	int weight;
	for (int i = 0; i < edges; i++) {
		fscanfResult = fscanf(inputFile, "%d %d %d", &from, &to, &weight);
		graph->edgeList[i * EDGE_MEMBERS] = from;
		graph->edgeList[i * EDGE_MEMBERS + 1] = to;
		graph->edgeList[i * EDGE_MEMBERS + 2] = weight;
	}

	fclose(inputFile);

	// EOF result of *scanf indicates an error
	if (fscanfResult == EOF) {
		printf("Something went wrong during reading of maze file, exiting!\n");
		exit(1);
	}
}

/*
 * sort the edges of the graph in parallel with mergesort in parallel
 */
void sort(WeightedGraph* graph) {
	int rank;
	int size;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Status status;

	// send number of elements
	int elements;
	if (rank == 0) {
		elements = graph->edges;
		MPI_Bcast(&elements, 1, MPI_INT, 0, MPI_COMM_WORLD);
	} else {
		MPI_Bcast(&elements, 1, MPI_INT, 0, MPI_COMM_WORLD);
	}

	// scatter the edges to sort
	int elementsPart = (elements + size - 1) / size;
	int* edgeListPart = (int*) malloc(
			elementsPart * EDGE_MEMBERS * sizeof(int));
	MPI_Scatter(graph->edgeList, elementsPart * EDGE_MEMBERS, MPI_INT,
			edgeListPart, elementsPart * EDGE_MEMBERS,
			MPI_INT, 0, MPI_COMM_WORLD);

	if (rank == size - 1 && elements % elementsPart != 0) {
		// number of elements and processes isn't divisible without remainder
		elementsPart = elements % elementsPart;
	}

	// sort the part
	mergeSort(edgeListPart, 0, elementsPart - 1);

	// merge all parts
	int from;
	int to;
	int elementsRecieved;
	for (int step = 1; step < size; step *= 2) {
		if (rank % (2 * step) == 0) {
			from = rank + step;
			if (from < size) {
				MPI_Recv(&elementsRecieved, 1, MPI_INT, from, 0,
				MPI_COMM_WORLD, &status);
				edgeListPart = realloc(edgeListPart,
						(elementsPart + elementsRecieved) * EDGE_MEMBERS
								* sizeof(int));
				MPI_Recv(&edgeListPart[elementsPart * EDGE_MEMBERS],
						elementsRecieved * EDGE_MEMBERS,
						MPI_INT, from, 0, MPI_COMM_WORLD, &status);
				merge(edgeListPart, 0, elementsPart + elementsRecieved - 1,
						elementsPart - 1);
				elementsPart += elementsRecieved;
			}
		} else if (rank % (step) == 0) {
			to = rank - step;
			MPI_Send(&elementsPart, 1, MPI_INT, to, 0, MPI_COMM_WORLD);
			MPI_Send(edgeListPart, elementsPart * EDGE_MEMBERS, MPI_INT, to, 0,
			MPI_COMM_WORLD);
		}
	}

	// edgeListPart is the new edgeList of the graph, cleanup other memory
	if (rank == 0) {
		free(graph->edgeList);
		graph->edgeList = edgeListPart;
	} else {
		free(edgeListPart);
	}
}

/*
 * merge the set of parent1 and parent2 with union by rank
 */
void unionSet(Set* set, const int parent1, const int parent2) {
	int root1 = findSet(set, parent1);
	int root2 = findSet(set, parent2);

	if (root1 == root2) {
		return;
	} else if (set->rank[root1] < set->rank[root2]) {
		set->canonicalElements[root1] = root2;
	} else if (set->rank[root1] > set->rank[root2]) {
		set->canonicalElements[root2] = root1;
	} else {
		set->canonicalElements[root1] = root2;
		set->rank[root2] = set->rank[root1] + 1;
	}
}
