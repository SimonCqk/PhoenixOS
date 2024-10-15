#include "autogen_common.h"

int main(int argc, char** argv) {
    int opt;
    const char *op_string = "d:s:g:";
    pos_retval_t retval = POS_SUCCESS;

    POSAutogener autogener;

    while((opt = getopt(argc, argv, op_string)) != -1){
        switch (opt){
        case 'd':
            // path to the header files
            autogener.header_directory = std::string(optarg);
            break;
        case 's':
            // path to the support files
            autogener.support_directory = std::string(optarg);
            break;
        case 'g':
            // path to generate the parser and worker logic
            autogener.gen_directory = std::string(optarg);
            break;
        default:
            POS_ERROR("unknown command line parameter: %s", op_string);
        }
    }

    // check whether all necessary parameters are provided
    if(unlikely(autogener.header_directory.size() == 0)){
        POS_ERROR("no header_directory provided with -s");
    }
    if(unlikely(autogener.support_directory.size() == 0)){
        POS_ERROR("no support_directory provided with -s");
    }
    if(unlikely(autogener.gen_directory.size() == 0)){
        POS_ERROR("no gen_directory provided with -g");
    }

    if(unlikely(
        POS_SUCCESS != (retval = autogener.collect_pos_support_yamls())
    )){
        POS_WARN("failed to collect PhOS support metadata");
        goto exit;
    }

    if(unlikely(
        POS_SUCCESS != (retval = autogener.collect_vendor_header_files())
    )){
        POS_WARN("failed to parse vendor headers: path(%s)", autogener.header_directory.c_str());
        goto exit;
    }

    if(unlikely(
        POS_SUCCESS != (retval = autogener.generate_pos_src())
    )){
        POS_WARN("failed to auto-generate source code");
        goto exit;
    }

exit:
    if(unlikely(retval != POS_SUCCESS))
        return -1;
    else
        return 0;
}