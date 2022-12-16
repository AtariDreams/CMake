/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmIncludeCommand.h"

#include <map>
#include <sstream>
#include <utility>

#include "cmExecutionStatus.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmMessageType.h"
#include "cmPolicies.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"

// cmIncludeCommand
bool cmIncludeCommand(std::vector<std::string> const& args,
                      cmExecutionStatus& status)
{
  static std::map<std::string, cmPolicies::PolicyID> DeprecatedModules;
  if (DeprecatedModules.empty()) {
    DeprecatedModules["Documentation"] = cmPolicies::CMP0106;
    DeprecatedModules["WriteCompilerDetectionHeader"] = cmPolicies::CMP0120;
  }

  if (args.empty() || args.size() > 4) {
    status.SetError("called with wrong number of arguments.  "
                    "include() only takes one file.");
    return false;
  }
  bool optional = false;
  bool noPolicyScope = false;
  std::string fname = args[0];
  std::string resultVarName;

  for (unsigned int i = 1; i < args.size(); i++) {
    if (args[i] == "OPTIONAL") {
      if (optional) {
        status.SetError("called with invalid arguments: OPTIONAL used twice");
        return false;
      }
      optional = true;
    } else if (args[i] == "RESULT_VARIABLE") {
      if (!resultVarName.empty()) {
        status.SetError("called with invalid arguments: "
                        "only one result variable allowed");
        return false;
      }
      if (++i < args.size()) {
        resultVarName = args[i];
      } else {
        status.SetError("called with no value for RESULT_VARIABLE.");
        return false;
      }
    } else if (args[i] == "NO_POLICY_SCOPE") {
      noPolicyScope = true;
    } else if (i > 1) // compat.: in previous cmake versions the second
                      // parameter was ignored if it wasn't "OPTIONAL"
    {
      std::string errorText =
        cmStrCat("called with invalid argument: ", args[i]);
      status.SetError(errorText);
      return false;
    }
  }

  if (fname.empty()) {
    status.GetMakefile().IssueMessage(
      MessageType::AUTHOR_WARNING,
      "include() given empty file name (ignored).");
    return true;
  }

  if (!cmSystemTools::FileIsFullPath(fname)) {
    bool system = false;
    // Not a path. Maybe module.
    std::string module = cmStrCat(fname, ".cmake");
    std::string mfile = status.GetMakefile().GetModulesFile(module, system);

    if (system) {
      auto ModulePolicy = DeprecatedModules.find(fname);
      if (ModulePolicy != DeprecatedModules.end()) {
        cmPolicies::PolicyStatus PolicyStatus =
          status.GetMakefile().GetPolicyStatus(ModulePolicy->second);
        switch (PolicyStatus) {
          case cmPolicies::WARN: {
            status.GetMakefile().IssueMessage(
              MessageType::AUTHOR_WARNING,
              cmStrCat(cmPolicies::GetPolicyWarning(ModulePolicy->second),
                       "\n"));
            CM_FALLTHROUGH;
          }
          case cmPolicies::OLD:
            break;
          case cmPolicies::REQUIRED_IF_USED:
          case cmPolicies::REQUIRED_ALWAYS:
          case cmPolicies::NEW:
            mfile = "";
            break;
        }
      }
    }

    if (!mfile.empty()) {
      fname = mfile;
    }
  }

  std::string fname_abs = cmSystemTools::CollapseFullPath(
    fname, status.GetMakefile().GetCurrentSourceDirectory());

  cmGlobalGenerator* gg = status.GetMakefile().GetGlobalGenerator();
  if (gg->IsExportedTargetsFile(fname_abs)) {
    const char* modal = nullptr;
    std::ostringstream e;
    MessageType messageType = MessageType::AUTHOR_WARNING;

    switch (status.GetMakefile().GetPolicyStatus(cmPolicies::CMP0024)) {
      case cmPolicies::WARN:
        e << cmPolicies::GetPolicyWarning(cmPolicies::CMP0024) << "\n";
        modal = "should";
        CM_FALLTHROUGH;
      case cmPolicies::OLD:
        break;
      case cmPolicies::REQUIRED_IF_USED:
      case cmPolicies::REQUIRED_ALWAYS:
      case cmPolicies::NEW:
        modal = "may";
        messageType = MessageType::FATAL_ERROR;
    }
    if (modal) {
      e << "The file\n  " << fname_abs
        << "\nwas generated by the export() "
           "command.  It "
        << modal
        << " not be used as the argument to the "
           "include() command.  Use ALIAS targets instead to refer to targets "
           "by alternative names.\n";
      status.GetMakefile().IssueMessage(messageType, e.str());
      if (messageType == MessageType::FATAL_ERROR) {
        return false;
      }
    }
    gg->CreateGenerationObjects();
    gg->GenerateImportFile(fname_abs);
  }

  std::string listFile = cmSystemTools::CollapseFullPath(
    fname, status.GetMakefile().GetCurrentSourceDirectory());

  const bool fileDoesnotExist = !cmSystemTools::FileExists(listFile);
  const bool fileIsDirectory = cmSystemTools::FileIsDirectory(listFile);
  if (fileDoesnotExist || fileIsDirectory) {
    if (!resultVarName.empty()) {
      status.GetMakefile().AddDefinition(resultVarName, "NOTFOUND");
    }
    if (optional) {
      return true;
    }
    if (fileDoesnotExist) {
      status.SetError(cmStrCat("could not find requested file:\n  ", fname));
      return false;
    }
    status.SetError(cmStrCat("requested file is a directory:\n  ", fname));
    return false;
  }

  bool readit =
    status.GetMakefile().ReadDependentFile(listFile, noPolicyScope);

  // add the location of the included file if a result variable was given
  if (!resultVarName.empty()) {
    status.GetMakefile().AddDefinition(
      resultVarName, readit ? fname_abs.c_str() : "NOTFOUND");
  }

  if (!optional && !readit && !cmSystemTools::GetFatalErrorOccurred()) {
    std::string m = cmStrCat("could not load requested file:\n  ", fname);
    status.SetError(m);
    return false;
  }
  return true;
}
