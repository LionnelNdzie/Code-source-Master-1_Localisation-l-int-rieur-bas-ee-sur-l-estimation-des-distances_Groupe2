RSSI based indoor localisation
==============================

Liste du matériel utilisé
=========================
- Ubuntu 12.04
- Contiki-2.7
- Cooja

instructions d'exploitation du code source/logiciel/script
==========================================================

Open a terminal and go to "<CONTIKI_HOME>/examples/rest-example/" directory.

MAIN EXAMPLE: rest-server-example-z1.c coap-client-example-coord coap-client-example-anchor1 coap-client-example-anchor2 : 

To run REST examples in COOJA on Linux
--------------------------------------------
In order to run this in the cooja simulator, theses are the steps to follow:
1 - copy the files named above in the MAIN EXAMPLE section from this repository to the <CONTIKI_HOME>/examples/rest-example/ folder
2 - open the cooja simulator
3 - Start a new simulation
4 - Create a z1 node by compiling the rest-server-example-z1.c source file
5 - Create another z1 node by compiling the coap-client-example-coord.c source file
6 - Do the same for the files coap-client-example-anchor1.c and coap-client-example-anchor2.c
7 - Start the simulation


To run REST server on real nodes (i.e. z1 motes)
--------------------------------------------

1. Program the first node (the dumb node) with the rest-server-example-z1

        make TARGET=z1 rest-server-example-z1.upload

2. Program the second node (the coordinator which also acts as an anchor)
		
		make TARGET=z1 coap-client-example-coord.upload
		
3. Program the third node (the first anchor)
		
		make TARGET=z1 coap-client-example-anchor1.upload
		
3. Program the fourth node (the second anchor)
		
		make TARGET=z1 coap-client-example-anchor2.upload
		
4. Run the python program contiki-viewer.py

		python contiki-viewer.py
		

Contact du candidat
===================
Tels: 693 562 499 / 653 87 97 10 / 690 90 72 06
