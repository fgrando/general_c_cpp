/*
 * read_file_cpp.cpp
 *
 *  Created on: 6 de fev. de 2021
 *      Author: fgrando
 */

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

using namespace std;

void read_example(){
    const char *filename = "demo.txt";

    ifstream file(filename, ios::in);
    if (!file) {
        cout << "failed to read '" << filename << "'" << endl;
        exit(1);
    }

    // read line by line
    string line;
    while (getline(file, line)) {
        cout << "read: " << line << endl;
    }

    // go back to the beginning of file
    file.clear();
    file.seekg(0);

    cout << endl << "parse the file lines" << endl;
    while (std::getline(file, line)) {
        istringstream iss(line);
        string word;
        int number;

        if (!(iss >> word >> number)) {
            cout << "breaking on parse failure" << endl;
            break;
        } else {
            cout << "parsed " << number << word << endl;
        }
    }

    // go back to the beginning of file
    file.clear();
    file.seekg(0);


    cout << endl << "parse the lines" << endl;
    while (getline(file, line)) {
        string word = "?";
        int number = -1;

        stringstream ss(line);
        ss >> word;
        ss >> number;

        cout << "w:" << word << " n:" << number << endl;
    }

    cout << endl << endl;
    file.close();
}

void write_example(){

    ofstream file("deleteme.txt");
    if (file) {
        file << "I wrote this file";
        cout << "file wrote!" << endl;
    }
    file.close();


    cout << "appending stuff" << endl;
    ofstream outfile;
    outfile.open("deleteme.txt", std::ios_base::app); // append instead of overwrite
    if (outfile) { outfile << "this is appended\nahaha!" << endl; }
    outfile.close();
}

void binary_example(){

    char buffer[100] = "this is a binary file!";

    ofstream myFile ("file.bin", ios::out | ios::binary);
    myFile.write (buffer, sizeof(buffer));
    myFile.close();

    fstream file ("file.bin", ios::in | ios::binary);
    char buffer2[150] = {0};
    if (!file.read(buffer2, sizeof(buffer2))){
        cout << "the error can be EOF!" << endl;
        cout << "read so far: [" << buffer2 << "]" << endl;
    }
    file.close();
}

void file_example() {
    read_example();
    cout << endl << endl;

    write_example();
    cout << endl << endl;

    binary_example();
    cout << endl << endl;
}
