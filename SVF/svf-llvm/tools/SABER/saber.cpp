//===- saber.cpp -- Source-sink bug checker------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===-----------------------------------------------------------------------===//

/*
 // Saber: Software Bug Check.
 //
 // Author: Yulei Sui,
 */

#include "SVF-LLVM/LLVMUtil.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "SABER/LeakChecker.h"
#include "SABER/FileChecker.h"
#include "SABER/DoubleFreeChecker.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"
#include "Util/Z3Expr.h"
#include "SABER/SaberCheckerAPI.h"          // 新增

using namespace llvm;
using namespace SVF;

int main(int argc, char ** argv)
{

    std::vector<std::string> moduleNameVec;
    moduleNameVec = OptionBase::parseOptions(
                        argc, argv, "Source-Sink Bug Detector", "[options] <input-bitcode...>"
                    );

    LLVMModuleSet::buildSVFModule(moduleNameVec);
    SVFIRBuilder builder;
    SVFIR* pag = builder.build();


       // ========== 新增：加载自定义 API 配置 ==========
    SaberCheckerAPI *ckAPI = SaberCheckerAPI::getCheckerAPI();


    std::string apiConfigFile = Options::CustomAPIConfig();   // 使用 operator() 获取值
if (!apiConfigFile.empty()) {
    if (!ckAPI->loadCustomAPIsFromFile(apiConfigFile)) {
        SVFUtil::errs() << "Failed to load custom API config: " << apiConfigFile << "\n";
        return 1;
    }
}




    std::unique_ptr<LeakChecker> saber;

    if(Options::MemoryLeakCheck())
        saber = std::make_unique<LeakChecker>();
    else if(Options::FileCheck())
        saber = std::make_unique<FileChecker>();
    else if(Options::DFreeCheck())
        saber = std::make_unique<DoubleFreeChecker>();
    else
        saber = std::make_unique<LeakChecker>();  // if no checker is specified, we use leak checker as the default one.

    saber->runOnModule(pag);
    LLVMModuleSet::releaseLLVMModuleSet();


    return 0;

}

