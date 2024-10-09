#include <Arduino.h>

recieveDatum() {
    // declaring temp string to store the curr "word" up to del
    String temp = "";
    String del = " ";
    int index = 0;
    for (int i = 0; i < information.length(); i++) {
        // If the current char is not del, append it to the current "word",
        // otherwise, you have completed the word, print it, and start a new word.
        if (information[i] != del[0]) {
            temp += information[i];
        } else {
            datum[index] = temp;
            index += 1;
            temp = "";
        }
    }

    for (int i = 0; i < 5; i++) {
        String word = datum[i];
        data_map[keys[i]] = word;
    }

    for (int i = 0; i < 5; i++) {
        String word = data_map[keys[i]];
        datum[i] = word;
    }

}

void sendDatum() {
    // declaring temp string to store the curr "word" up to del
    String full = "";
    information = full;
    String temp = " ";
    for (int i = 0; i < 5; i++) {
        full = full + datum[i];
        full = full + temp;
    }
    information = full;
}