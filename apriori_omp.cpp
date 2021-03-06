#include <omp.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <algorithm>
#include <sys/time.h>
using namespace std;

const float MIN_CONFIDENCE = 1.;

void read_file(char file_name[], vector< vector<string> > &matrix, map<string,float> &dictionary);
void find_itemsets(vector<string> matrix, vector<string> candidates, map<string,float> &temp_dictionary, int k, int item_idx, string itemset, int current, vector<string> single_candidates);
void prune_itemsets(map<string,float> &temp_dictionary, vector<string> &candidates, float min_support, vector<string> &single_candidates);
void update_candidates(vector<string> &candidates, vector<string> freq_itemsets, vector<string> &single_candidates);
void compute_combinations(int offset, int k, vector<string> &elements, vector<string> &items, vector<string> &combinations);
void generate_association_rules(map<string,float> dictionary, float min_confidence);
string create_consequent(string antecedent, vector<string> items);

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char* argv[]){
    char* file_name = argv[1];
    float min_support = atof(argv[2]);
    vector< vector<string> > matrix;
    map<string,float> dictionary;
    map<string,float> temp_dictionary;
    vector<string> candidates;
    vector<string> single_candidates;
    int n_rows;
    string item;

    cout<<"Max threads: "<<omp_get_max_threads()<<endl;

    struct timeval start, end;
    double elapsed;

    gettimeofday(&start, NULL);

    // read file into 2D vector matrix and insert 1-itemsets in dictionary as key with their frequency as value
    read_file(file_name, matrix, dictionary);

    n_rows = matrix.size();

    // divide frequency by number of rows to calculate support
    #pragma omp parallel for
    for (int i=0; i<dictionary.size(); i++) {
        map<string, float>::iterator itr = dictionary.begin();
        advance(itr, i);
        itr->second = itr->second/float(n_rows);
    }

    // prune from dictionary 1-itemsets with support < min_support and insert items in candidates vector
    prune_itemsets(dictionary, candidates, min_support, single_candidates);

    // insert in dictionary all k-itemset
    int n = 2; // starting from 2-itemset
    while(!candidates.empty()){
        temp_dictionary.clear();
        // read matrix and insert n-itemsets in temp_dictionary as key with their frequency as value
        #pragma omp parallel for
        for (int i = 0; i < matrix.size(); i++){
            find_itemsets(matrix[i], candidates, temp_dictionary, n, -1, "", 0, single_candidates);
        }
        // divide frequency by number of rows to calculate support
        #pragma omp parallel for
        for (int i=0; i<temp_dictionary.size(); i++) {
            map<string, float>::iterator itr = temp_dictionary.begin();
            advance(itr, i);
            itr->second = itr->second/float(n_rows);
        }
        // prune from temp_dictionary n-itemsets with support < min_support and insert items in candidates vector
        prune_itemsets(temp_dictionary, candidates, min_support, single_candidates);
        // append new n-itemsets to main dictionary
        dictionary.insert(temp_dictionary.begin(), temp_dictionary.end());
        n++;
    }

    gettimeofday(&end, NULL);
    elapsed = (end.tv_sec - start.tv_sec) + 
              ((end.tv_usec - start.tv_usec)/1000000.0);
    cout<<"Time passed: "<<elapsed<<endl;

    cout<<"KEY\tVALUE\n";
    for (map<string, float>::iterator itr = dictionary.begin(); itr != dictionary.end(); ++itr) {
        cout << itr->first << '\t' << itr->second << '\n';
    }

    // print out all association rules with confidence >= min_confidence
    // generate_association_rules(dictionary, MIN_CONFIDENCE);

    return 0;
}

// ------------------------------------------------------------
// Functions
// ------------------------------------------------------------

void read_file(char file_name[], vector< vector<string> > &matrix, map<string,float> &dictionary){
    ifstream myfile (file_name);

    vector<string> row;    
    string line;
    stringstream ss;
    string item;

    while(getline (myfile, line)){
        ss << line;

        while(getline (ss, item, ' ')) {
            item.erase(remove(item.begin(), item.end(), '\r'), item.end());
            row.push_back(item);
            // insert item into dictionary and increment its value
            dictionary[item]++;
        }

        sort(row.begin(), row.end());
        matrix.push_back(row);

        ss.clear();
        row.clear();
    }

    myfile.close();
}

void find_itemsets(vector<string> matrix, vector<string> candidates, map<string,float> &temp_dictionary, int k, int item_idx, string itemset, int current, vector<string> single_candidates){
    if(current == k){
        itemset = itemset.erase(0,1); // remove first space

        // if itemset is a candidate insert it into temp_dictionary to calculate support 
        if(find(candidates.begin(), candidates.end(), itemset) != candidates.end()){

            #pragma omp critical
            temp_dictionary[itemset]++;
            return;
        }
        // if itemset is not a candidate discard it
        else{
            return;
        }
    }
    
    string item;
    for (int j = ++item_idx; j < matrix.size(); j++){
        item = matrix[j];

        // if item does not compose one of the candidates, skip it
        if(!(find(single_candidates.begin(), single_candidates.end(), item) != single_candidates.end())){
            continue;
        }

        find_itemsets(matrix, candidates, temp_dictionary, k, j, itemset + " " + item, current+1, single_candidates);
    }
}

void prune_itemsets(map<string,float> &temp_dictionary, vector<string> &candidates, float min_support, vector<string> &single_candidates){
    vector<string> freq_itemsets;
    candidates.clear(); // empty candidates to then update it
    single_candidates.clear();

    // too many complications to parallelize
    for (map<string, float>::iterator it = temp_dictionary.begin(); it != temp_dictionary.end(); ){ // like a while
        if (it->second < min_support){
            temp_dictionary.erase(it++);
        }
        else{
            freq_itemsets.push_back(it->first);
            ++it;
        }
    }

    if(!freq_itemsets.empty()){
        update_candidates(candidates, freq_itemsets, single_candidates);
    }
}

void update_candidates(vector<string> &candidates, vector<string> freq_itemsets, vector<string> &single_candidates){
    string item;
    stringstream to_combine;
    vector<string> items;
    vector<string> elements;
    string combination;

    int common_items;

    #pragma omp parallel for private(item, to_combine, items, elements, combination, common_items)
    for(int i = 0; i < freq_itemsets.size()-1; i++){
        for(int j = i+1; j < freq_itemsets.size(); j++){
            common_items = 0;
            items.clear();
            to_combine.clear();
            combination.clear();

            to_combine << freq_itemsets[i] + ' ' + freq_itemsets[j];
            while(getline (to_combine, item, ' ')) {
                if(!(find(items.begin(), items.end(), item) != items.end())){
                    items.push_back(item);
                    combination += " " + item;
                }
                else{
                    common_items++;
                }
            }

            // if the statement is true than we can add combination as candidate
            // else we created all correct combinations and we pass to the next itemset
            if(common_items == items.size()-2){
                combination.erase(0,1); // remove first space

                #pragma omp critical
                {   
                    candidates.push_back(combination);
                    
                    // insert single items candidates
                    for(int i=0; i<items.size(); i++) {
                        if(!(find(single_candidates.begin(), single_candidates.end(), items[i]) != single_candidates.end())){
                            single_candidates.push_back(items[i]);
                        }
                    }
                    
                }
            }
            else{
                break;
            }

        }
    }
}

// https://stackoverflow.com/questions/12991758/creating-all-possible-k-combinations-of-n-items-in-c/28698654
void compute_combinations(int offset, int k, vector<string> &elements, vector<string> &items, vector<string> &combinations) {
    if (k == 0){
        string temp;
        for (int i = 0; i < elements.size(); ++i){
            temp += " " + elements[i];
        }
        temp.erase(0,1); // remove first space
        combinations.push_back(temp);
        return;
    }
    for (int i = offset; i <= items.size() - k; ++i) {
        elements.push_back(items[i]);
        compute_combinations(i+1, k-1, elements, items, combinations);
        elements.pop_back();
    }
}

void generate_association_rules(map<string,float> dictionary, float min_confidence){
    stringstream ss;
    vector<string> items;
    vector<string> elements;
    vector<string> combinations;
    string item;
    string antecedent;
    string consequent;
    float conf;

    vector<string>::iterator it_items;
    
    // iterate through all frequent itemsets
    for (map<string, float>::iterator i = dictionary.begin(); i != dictionary.end(); ++i) {
        if(!(i->first.find(' ') != string::npos)) continue; // if itemset has just 1 element continue

        cout<<"---------------"<<endl;
        cout<<"\e[1m"<<i->first<<"\e[0m"<<endl;
        cout<<"---------------"<<endl;

        // insert all items of the itemset into a vector
        ss << i->first;
        while(getline (ss, item, ' ')) {
            items.push_back(item);
        }

        // compute all combinations (which will be the antecedents) of size from k-1 to 1 among items of the itemsets
        for (int idx = items.size()-1; idx>0; idx--){
            compute_combinations(0, idx, elements, items, combinations);
        }

        for (vector<string>::iterator itr = combinations.begin(); itr != combinations.end(); ++itr){
            antecedent = *itr;
            consequent = create_consequent(antecedent, items);

            conf = float(dictionary[i->first])/float(dictionary[antecedent]);
            if(conf >= min_confidence){
                cout<<antecedent<<" -> "<<consequent<<" : "<<conf<<endl;
            }
        }

        ss.clear();
        items.clear();
        combinations.clear();
    }
}

string create_consequent(string antecedent, vector<string> items){
    stringstream ss;
    string item;
    string consequent;
    vector<string>::iterator itr;

    ss << antecedent;

    while(getline (ss, item, ' ')) {
        itr = find(items.begin(), items.end(), item);

        if(itr != items.end()){
            items.erase(itr);
        }
    }
    
    for (itr = items.begin(); itr != items.end(); ++itr){
        consequent += " " + *itr;
    }
    consequent.erase(0,1);

    return consequent;
}