#ifndef JELLI_TEST_COLLECTION_CATALOG_FIXTURE_H
#define JELLI_TEST_COLLECTION_CATALOG_FIXTURE_H
#include "jelli/collection.h"
/* Full authored catalogs replace the generated production objects at link time. */
const unsigned jelli_prize_count = 16u;
const JelliPrizeDefinition jelli_prizes[16] = {{"test", "TEST", 11001u}};
const JelliCollectionForm jelli_collection_forms[] = {{1201u, "TEST"}};
const unsigned jelli_collection_form_count = 1u;
const JelliEvolutionSet jelli_evolution_sets[] = {{1u, {0u, 0u}, 0u}};
const unsigned jelli_evolution_set_count = 1u;
const unsigned jelli_collection_entry_count = 16u;
const JelliCollectionEntry jelli_collection_entries[] = {
    {1u, 0u, 1u, "ONE", "START"},        {2u, 2u, 1u, "TWO", "CATCH"},
    {3u, 3u, 1u, "THREE", "CATCH"},      {4u, 4u, 1u, "FOUR", "CATCH"},
    {5u, 5u, 1u, "FIVE", "CATCH"},       {6u, 6u, 1u, "SIX", "CATCH"},
    {7u, 7u, 1u, "SEVEN", "CATCH"},      {8u, 8u, 1u, "EIGHT", "CATCH"},
    {9u, 9u, 1u, "NINE", "CATCH"},       {10u, 10u, 1u, "TEN", "CATCH"},
    {11u, 11u, 1u, "ELEVEN", "CATCH"},   {12u, 12u, 1u, "TWELVE", "CATCH"},
    {13u, 13u, 1u, "THIRTEEN", "CATCH"}, {14u, 14u, 1u, "FOURTEEN", "CATCH"},
    {15u, 15u, 1u, "FIFTEEN", "CATCH"},  {16u, 16u, 1u, "SIXTEEN", "CATCH"}};
const JelliNewborn jelli_collection_newborn = {
    {500u, 700u, 700u, 500u, 500u}, 100u, 700u, 22u, 288000u, 324000u};
const JelliStartingItems jelli_collection_start = {5u, 3u};

#endif
