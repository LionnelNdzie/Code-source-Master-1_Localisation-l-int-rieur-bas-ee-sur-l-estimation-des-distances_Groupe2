!/usr/bin/python
import serial
import re
PORT ='/dev/ttyUSB1'
ser = serial.Serial(
	port=PORT,\
	baudrate=115200,\
	parity=serial.PARITY_NONE,\
	stopbits=serial.STOPBITS_ONE,\
	bytesize=serial.EIGHTBITS,\
	timeout=0)

print("connected to: " + ser.portstr)

# The parameter raw_rssis is like this: "Python:0:-29;1:-30;2:-30;"
def retreive_rssis(raw_rssis):
	raw_rssis = re.sub("Python", "", raw_rssis) # We remove the leading "Python"
	values = re.split(";", raw_rssis, 2); # We create a new array by spliting the input string every time there's ";"

	for i in range(3): # For each ":sender_id:value;"
		values[i] = re.sub("^:", "", values[i]); # We remove the leading ":"
		values[i] = re.sub(";", "", values[i]); # We remove the trailing ";"

	key_list = ["coord", "anchor1", "anchor2"]
	rssis = dict(zip(key_list, [None]*len(key_list))) # We create a dictionnary with keys = key_list and values are null

	for i in range(3): # For each "sender_id:value"
		temp = re.split(":", values[i]) # We create a temporary array temp where temp[0] = sender_id and temp[1] = value
		if(temp[0] == "0"): # sender_id = 0 means it is the coordinator
			rssis["coord"] = temp[1]
		elif(temp[0] == "1"): # sender_id = 1 means it is the first anchor
			rssis["anchor1"] = temp[1]
		else: # otherwise, it is the second anchor
			rssis["anchor2"] = temp[1]

	return rssis
	
# The parameter rssis_and_senders is like this: {'anchor2': '-30', 'coord': '-29', 'anchor1': '-30'}
def retreive_distances_from_rssis(rssis_and_senders):
	key_list = ["coord", "anchor1", "anchor2"]
	distances = dict(zip(key_list, [None]*len(key_list)))
	
	for key, val in rssis_and_senders.items():
		distances[key] = 0
		
	return distances
		
	
rssis = retreive_rssis(pattern)
print(rssis)

distances = retreive_distances_from_rssis(rssis)
print(distances)

while True:
	line = ser.readline()
	if line:
		if(re.search("^Python.*;$", line)):
			print(line),

ser.close()

