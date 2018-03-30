/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef PARSER_H
#define PARSER_H

#include "Module.h"

namespace vc4c
{
    enum class MetaDataType
    {
        ARG_ADDR_SPACES,
        ARG_ACCESS_QUALIFIERS,
        ARG_TYPE_NAMES,
        ARG_TYPE_QUALIFIERS,
        ARG_NAMES,
        WORK_GROUP_SIZES,
        WORK_GROUP_SIZES_HINT
    };

    template <>
    struct hash<MetaDataType> : public std::hash<uint8_t>
    {
        size_t operator()(const MetaDataType& val) const noexcept
        {
            return std::hash<uint8_t>::operator()(static_cast<uint8_t>(val));
        }
    };

    /*
     * Base class for any front-end implementation converting an input to a module in internal representation
     */
    class Parser
    {
    public:
        virtual ~Parser();
        /*
         * Parses the currently set input and populates the module given with the globals/functions extracted from the
         * input.
         */
        virtual void parse(Module& module) = 0;
    };
} // namespace vc4c

#endif /* PARSER_H */
