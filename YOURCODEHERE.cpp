#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <math.h>
#include <fcntl.h>
#include <vector>
#include <iterator>

#include "431project.h"

using namespace std;

/*
 * Enter your PSU IDs here to select the appropriate scanning order.
 */
#define PSU_ID_SUM (912345679+911111111)

/*
 * Some global variables to track heuristic progress.
 * 
 * Feel free to create more global variables to track progress of your
 * heuristic.
 */
unsigned int currentlyExploringDim = 0;
bool currentDimDone = false;
bool isDSEComplete = false;

/*
 * Given a half-baked configuration containing cache properties, generate
 * latency parameters in configuration string. You will need information about
 * how different cache paramters affect access latency.
 * 
 * Returns a string similar to "1 1 1"
 */
std::string generateCacheLatencyParams(string halfBackedConfig) {

	std::stringstream latencySettings; // Order is D I U.

	unsigned int dl1sets = 32 << extractConfigPararm(halfBackedConfig, 3);
	unsigned int dl1assoc = 1 << extractConfigPararm(halfBackedConfig, 4);
	unsigned int dl1blocksize = 8 * (1 << extractConfigPararm(halfBackedConfig, 2));

    int d1ExponentValue = (dl1assoc * dl1sets * dl1blocksize) / 1024; // Exponent value to pass to log base 2.
    int d1LatencyValue = log2(d1ExponentValue); // Log value to pass for latency settings. 
    int d1AssociateValue = extractConfigPararm(halfBackedConfig, 4); // Get the index value for the associate D1 cache.

    if (d1AssociateValue == 1) { // If the index value is 1, add 1 to the latency.
        d1LatencyValue = d1LatencyValue + 1; 
    } else if (d1AssociateValue == 2) { // Repeat for 2.
        d1LatencyValue = d1LatencyValue + 2;
    }

    latencySettings << (d1LatencyValue - 1) <<" "; // Pass value into latency settings.

	unsigned int il1sets = 32 << extractConfigPararm(halfBackedConfig, 5);
	unsigned int il1assoc = 1 << extractConfigPararm(halfBackedConfig, 6);
	unsigned int il1blocksize = 8 * (1 << extractConfigPararm(halfBackedConfig, 2));

    int i1ExponentValue = (il1assoc * il1sets * il1blocksize) / 1024; // Exponent value to pass for il1. 
    int i1LatencyValue = log2(i1ExponentValue);
    int i1AssociativeValue = extractConfigPararm(halfBackedConfig, 6);

    if (i1AssociativeValue == 1) {
        i1LatencyValue = i1LatencyValue + 1;
    } else if (i1AssociativeValue == 2) {
        i1LatencyValue = i1LatencyValue + 2;
    }

    latencySettings << (i1LatencyValue - 1) << " ";

	unsigned int l2sets = 256 << extractConfigPararm(halfBackedConfig, 7);
	unsigned int l2blocksize = 16 << extractConfigPararm(halfBackedConfig, 8);
	unsigned int l2assoc = 1 << extractConfigPararm(halfBackedConfig, 9);
	
    int l2ExponentValue = (l2assoc * l2sets * l2blocksize) / 1024; // Exponent value to pass for l2. 
    int l2LatencyValue = log2(l2LatencyValue); 
    int l2AssociativeValue = extractConfigPararm(halfBackedConfig, 9);

    if (l2AssociativeValue == 1) {
        l2LatencyValue = l2LatencyValue + 1;
    } else if (l2AssociativeValue == 2) {
        l2LatencyValue = l2LatencyValue + 2;
    } else if (l2AssociativeValue == 3) {
        l2LatencyValue = l2LatencyValue + 3;
    } else if (l2AssociativeValue == 4) {
        l2LatencyValue = l2LatencyValue + 4;
    }

    latencySettings << (l2LatencyValue - 5);

    return latencySettings.str();
}

/*
 * Returns 1 if configuration is valid, else 0
 */
int validateConfiguration(std::string configuration) {

	// FIXME - YOUR CODE HERE

	//Check 1
	int il1BLockSize = pow(2, extractConfigPararm(configuration, 2))*8;
	int iFetchQueueSize = pow(2, extractConfigPararm(configuration, 0))*8;
	if(il1BLockSize < iFetchQueueSize){
		return 0;
	}


	//Check 2
	int ul2BLockSize = pow(2, extractConfigPararm(configuration, 8) + 1)*8;	
	if(ul2BLockSize < 2*il1BLockSize || ul2BLockSize > 128){
		return 0;
	}
	
	//Check 3
	unsigned int il1sets = 32 << extractConfigPararm(configuration, 5);
	unsigned int il1assoc = 1 << extractConfigPararm(configuration, 6);
	unsigned int il1blocksize = 8 * (1 << extractConfigPararm(configuration, 2));

	int il1Size = il1assoc * il1sets * il1blocksize;

	unsigned int dl1sets = 32 << extractConfigPararm(configuration, 3);
	unsigned int dl1assoc = 1 << extractConfigPararm(configuration, 4);
	unsigned int dl1blocksize = 8 * (1 << extractConfigPararm(configuration, 2));

	int dl1Size = dl1assoc * dl1sets * dl1blocksize;

	if(il1Size < pow(2, 11) || dl1Size < pow(2, 11) || il1Size > pow(2,16) || dl1Size > pow(2,16)){
		return 0;
	}

	//Check 4
	unsigned int l2sets = 256 << extractConfigPararm(configuration, 7);
	unsigned int l2blocksize = 16 << extractConfigPararm(configuration, 8);
	unsigned int l2assoc = 1 << extractConfigPararm(configuration, 9);

	int l2Size = l2assoc * l2sets * l2blocksize;

	if(l2Size < pow(2,15) || l2Size > pow(2, 20)){
		return 0;
	}

	// The below is a necessary, but insufficient condition for validating a
	// configuration.
	return isNumDimConfiguration(configuration);
}

/*
 * Given the current best known configuration, the current configuration,
 * and the globally visible map of all previously investigated configurations,
 * suggest a previously unexplored design point. You will only be allowed to
 * investigate 1000 design points in a particular run, so choose wisely.
 *
 * In the current implementation, we start from the leftmost dimension and
 * explore all possible options for this dimension and then go to the next
 * dimension until the rightmost dimension.
 */
std::string generateNextConfigurationProposal(std::string currentconfiguration,
		std::string bestEXECconfiguration, std::string bestEDPconfiguration,
		int optimizeforEXEC, int optimizeforEDP) {

	//
	// Some interesting variables in 431project.h include:
	//
	// 1. GLOB_dimensioncardinality
	// 2. GLOB_baseline
	// 3. NUM_DIMS
	// 4. NUM_DIMS_DEPENDENT
	// 5. GLOB_seen_configurations

	std::string nextconfiguration = currentconfiguration;
	// Continue if proposed configuration is invalid or has been seen/checked before.
	while (!validateConfiguration(nextconfiguration) ||
		GLOB_seen_configurations[nextconfiguration]) {

		// Check if DSE has been completed before and return current
		// configuration.
		if(isDSEComplete) {
			return currentconfiguration;
		}

		std::stringstream ss;

		string bestConfig;
		if (optimizeforEXEC == 1)
			bestConfig = bestEXECconfiguration;

		if (optimizeforEDP == 1)
			bestConfig = bestEDPconfiguration;

		// Fill in the dimensions already-scanned with the already-selected best
		// value.
		for (int dim = 0; dim < currentlyExploringDim; ++dim) {
			ss << extractConfigPararm(bestConfig, dim) << " ";
		}

		// Handling for currently exploring dimension. This is a very dumb
		// implementation.
		int nextValue = extractConfigPararm(nextconfiguration,
				currentlyExploringDim) + 1;

		if (nextValue >= GLOB_dimensioncardinality[currentlyExploringDim]) {
			nextValue = GLOB_dimensioncardinality[currentlyExploringDim] - 1;
			currentDimDone = true;
		}

		ss << nextValue << " ";

		// Fill in remaining independent params with 0.
		for (int dim = (currentlyExploringDim + 1);
				dim < (NUM_DIMS - NUM_DIMS_DEPENDENT); ++dim) {
			ss << "0 ";
		}

		//
		// Last NUM_DIMS_DEPENDENT3 configuration parameters are not independent.
		// They depend on one or more parameters already set. Determine the
		// remaining parameters based on already decided independent ones.
		//
		string configSoFar = ss.str();

		// Populate this object using corresponding parameters from config.
		ss << generateCacheLatencyParams(configSoFar);

		// Configuration is ready now.
		nextconfiguration = ss.str();

		// Make sure we start exploring next dimension in next iteration.
		if (currentDimDone) {
			currentlyExploringDim++;
			currentDimDone = false;
		}

		// Signal that DSE is complete after this configuration.
		if (currentlyExploringDim == (NUM_DIMS - NUM_DIMS_DEPENDENT))
			isDSEComplete = true;
	}
	return nextconfiguration;
}

