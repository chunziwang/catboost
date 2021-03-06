#pragma once

#include <library/getopt/small/last_getopt.h>
#include <library/json/json_value.h>

#include <util/generic/string.h>
#include <util/string/iterator.h>

#include <catboost/libs/helpers/exception.h>
#include <catboost/libs/options/load_options.h>
#include <catboost/libs/options/cat_feature_options.h>


// exposed for TAnalyticalModeCommonParams::BindParserOpts
void BindDsvPoolFormatParams(NLastGetopt::TOpts* parser,
                               NCatboostOptions::TDsvPoolFormatParams* dsvPoolFormatParams);


void ParseCommandLine(int argc, const char* argv[],
                      NJson::TJsonValue* plainJsonPtr,
                      TString* paramPath,
                      NCatboostOptions::TPoolLoadParams* params);


TVector<TString> ReadClassNames(const TString& modelInfoParams);
