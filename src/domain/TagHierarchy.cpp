#include "TagHierarchy.h"

#include "UnicodeCaseFold.h"

bool TagHierarchy::isSelfOrDescendant(const std::string& name, const std::string& ancestorName)
{
    const std::string foldedName = UnicodeCaseFold::foldUtf8(name);
    const std::string foldedAncestor = UnicodeCaseFold::foldUtf8(ancestorName);

    if (foldedName == foldedAncestor)
    {
        return true;
    }

    const std::string prefix = foldedAncestor + "/";
    return foldedName.size() > prefix.size()
        && foldedName.compare(0, prefix.size(), prefix) == 0;
}
