/*
 * This program will read a single Java Class File and print out the class'
 * dependencies and exports, as requested by flags.
 */

#include <netinet/in.h>

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "csapp.h"

/* The magic number that must be the first four bytes of a valid JCF. */
#define JCF_MAGIC       0xCAFEBABE

/* 
 * The header of the Java class file.
 * The __attribute__((packed)) after the structure definition tells the
 * compiler to not add any padding to the structure for alignment.  This
 * allows this structure to be read directly from a packed file.
 */
struct jcf_header {
        uint32_t        magic;
        uint16_t        minor_version;
        uint16_t        major_version;
} __attribute__((packed));

/* The body of the Java class file. */
struct jcf_body {
        uint16_t        access_flags;
        uint16_t        this_class;
        uint16_t        super_class;
} __attribute__((packed));

/* An enumeration of the constant tags. */
enum jcf_cp_tags {
        JCF_CONSTANT_Class = 7,
        JCF_CONSTANT_Fieldref = 9,
        JCF_CONSTANT_Methodref = 10,
        JCF_CONSTANT_InterfaceMethodref = 11,
        JCF_CONSTANT_String = 8,
        JCF_CONSTANT_Integer = 3,
        JCF_CONSTANT_Float = 4,
        JCF_CONSTANT_Long = 5,
        JCF_CONSTANT_Double = 6,
        JCF_CONSTANT_NameAndType = 12,
        JCF_CONSTANT_Utf8 = 1
};

/* An enumeration of the access flags. */
enum jcf_access_flags {
        JCF_ACC_PUBLIC = 0x0001,
        JCF_ACC_PRIVATE = 0x0002,
        JCF_ACC_PROTECTED = 0x0004,
        JCF_ACC_STATIC = 0x0008,
        JCF_ACC_FINAL =0x0010,
        JCF_ACC_SYNCHRONIZED = 0x0020,
        JCF_ACC_VOLATILE = 0x0040,
        JCF_ACC_TRANSIENT = 0x0080,
        JCF_ACC_NATIVE = 0x0100,
        JCF_ACC_INTERFACE = 0x0200,
        JCF_ACC_ABSTRACT = 0x0400,
        JCF_ACC_STRICT = 0x0800
};

/* 
 * The generic constant pool info structure. 
 * This structure should be thought of as an abstract class that never
 * should be allocated directly. 
 *
 * The zero length array "info" in this structure is a special design
 * pattern in C that allows for variable length structures.  This is
 * done by casting another structure to this general type or by
 * allocating (sizeof(struct foo) + array_len) bytes.  This causes
 * memory to be allocated for the array, which can then be accessed
 * normally.
 */
struct jcf_cp_info {
        uint8_t         tag;
        uint8_t         info[0];
} __attribute__((packed));

/* 
 * The generic constant pool info structure for all constants that have
 * a single u2. 
 */
struct jcf_cp_info_1u2 {
        uint8_t         tag;
        uint16_t        u2;
} __attribute__((packed));

/* 
 * The generic constant pool info structure for all constants that have
 * two u2s. 
 */
struct jcf_cp_info_2u2 {
        uint8_t         tag;
        struct {
                uint16_t        u2_1;
                uint16_t        u2_2;
        } body;
} __attribute__((packed));

/* 
 * The generic constant pool info structure for all constants that have
 * a single u4. 
 */
struct jcf_cp_info_1u4 {
        uint8_t         tag;
        uint32_t        u4;
} __attribute__((packed));

/* The constant pool info for classes. */
struct jcf_cp_class_info {
        uint8_t         tag;
        uint16_t        name_index;
} __attribute__((packed));

/* 
 * The constant pool info for all reference constants.
 * These are any of the following constants: JCF_CONSTANT_Fieldref,
 * JCF_CONSTANT_Methodref, and JCF_CONSTANT_InterfaceMethodref.
 */
struct jcf_cp_ref_info {
        uint8_t         tag;
        uint16_t        class_index;
        uint16_t        name_and_type_index;
} __attribute__((packed));

/* The constant pool info for name and type tuples. */
struct jcf_cp_nameandtype_info {
        uint8_t         tag;
        uint16_t        name_index;
        uint16_t        descriptor_index;
} __attribute__((packed));

/*
 * The constant pool info for UTF8 strings.
 * 
 * This structure should be created by allocating
 * (sizeof(struct jcf_cp_utf8_info) + array_len) bytes.
 */
struct jcf_cp_utf8_info {
        uint8_t         tag;
        uint16_t        length;
        uint8_t         bytes[0];
} __attribute__((packed));

/* A field info entry for the class. */
struct jcf_field_info {
        uint16_t        access_flags;
        uint16_t        name_index;
        uint16_t        descriptor_index;
} __attribute__((packed));

/* A method info entry for the class. */
struct jcf_method_info {
        uint16_t        access_flags;
        uint16_t        name_index;
        uint16_t        descriptor_index;
} __attribute__((packed));

/* A structure for holding the constant pool. */
struct jcf_constant_pool {
        uint16_t        count;
        struct jcf_cp_info **pool;
};

/* A structure for holding processing state. */
struct jcf_state {
        FILE            *f;
        bool            depends_flag;
        bool            exports_flag;
        bool            verbose_flag;
        struct jcf_constant_pool constant_pool;
};

/* Local Function Prototypes */
static void readjcf_error(void);
static int print_jcf_constant(struct jcf_state *jcf,
    uint16_t index, uint8_t expected_tag);
static int process_jcf_header(struct jcf_state *jcf);
static int process_jcf_constant_pool(struct jcf_state *jcf);
static void destroy_jcf_constant_pool(struct jcf_constant_pool *pool);
static int process_jcf_body(struct jcf_state *jcf);
static int process_jcf_interfaces(struct jcf_state *jcf);
static int process_jcf_fields(struct jcf_state *jcf);
static int process_jcf_methods(struct jcf_state *jcf);
static int process_jcf_fields_and_methods_helper(struct jcf_state *jcf);
static int process_jcf_attributes(struct jcf_state *jcf);

/*
 * Requires:
 *  Nothing.
 *
 * Effects:
 *  Prints a formatted error message to stderr.
 */
static void
readjcf_error(void)
{
        fprintf(stderr, "ERROR: Unable to process file!\n");
}

/*
 * Requires:
 *  The constant pool must be initialized.
 *
 * Effects:
 *  If the index is valid and points to a constant of the expected type,
 *  this function will print the constant and return 0.  Otherwise, -1
 *  is returned.
 */
static int
print_jcf_constant(struct jcf_state *jcf, uint16_t index,
    uint8_t expected_tag)
{
        assert(jcf != NULL);

        /* Verify the index. */
        if (jcf->constant_pool.count <= index || index < 1) {
                printf("index mismatch\n");
                return (-1);
        }
        struct jcf_cp_info *info = jcf->constant_pool.pool[index];
        /* Verify the tag. */
        if (expected_tag != info->tag) {
                printf("tag mismatch\n");
                return (-1);
        }
        /* Print the constant. */
        switch (info->tag) {
        case JCF_CONSTANT_Class: {
                /* Print the class. */
                struct jcf_cp_info_1u2 *class_info = 
                    (struct jcf_cp_info_1u2 *) info;
                print_jcf_constant(jcf, class_info->u2, JCF_CONSTANT_Utf8);
                break; 
        }
        case JCF_CONSTANT_Fieldref:
        case JCF_CONSTANT_Methodref:
        case JCF_CONSTANT_InterfaceMethodref: {
                /* 
                 * Print the reference, with the Class and NameAndType
                 * separated by a '.'.
                 */
                struct jcf_cp_info_2u2 *ref_info = 
                        (struct jcf_cp_info_2u2 *) info;
                print_jcf_constant(jcf, ref_info->body.u2_1,
                                   JCF_CONSTANT_Class);
                printf(".");
                print_jcf_constant(jcf, ref_info->body.u2_2, 
                        JCF_CONSTANT_NameAndType);
                break; 
        }
        case JCF_CONSTANT_NameAndType: {
                /* Print the name and type. */
                /* 
                 * name_index and descriptor_index fields  are indices 
                 * into the constant pool that correspond to 
                 * CONSTANT Utf8 constants.
                 */
                struct jcf_cp_info_2u2 *n_t_info = 
                        (struct jcf_cp_info_2u2 *) info;
                print_jcf_constant(jcf, n_t_info->body.u2_1,
                                   JCF_CONSTANT_Utf8);
                printf(" ");
                print_jcf_constant(jcf, n_t_info->body.u2_2, 
                        JCF_CONSTANT_Utf8);
                break; 
        }
        case JCF_CONSTANT_Utf8: { 

                /* Print the UTF8. */
                struct jcf_cp_utf8_info *utf8_info = 
                        (struct jcf_cp_utf8_info *) info;
                uint16_t length = utf8_info->length; 

                /* one additional space for NULL terminator */
                uint8_t *string = malloc(length + 1);
                int i;
                uint8_t *data = utf8_info->bytes;
                for (i = 0; i < length; i++) {
                        string[i] = data[i];
                } 
        
                /* NUL-terminate the strings */
                string[length] = '\0'; 
                fprintf(stdout, "%s", string); 
                free(string); 

                break; 
        }
        default:
                /* Ignore all other constants. */
                break;
        }
        return (0);
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.
 *
 * Effects:
 *  Reads and verifies the Java class file header from file "jcf.f".
 *  Returns 0 on success and -1 on failure.
 */
static int
process_jcf_header(struct jcf_state *jcf)
{
        assert(jcf != NULL);
        uint32_t magic;
        struct jcf_header header_struct;
        /* Read the header. */
        /* 
         * size_t object (value fread returned) should equal the nmemb 
         * parameter (1, in this case) 
         */
        if (fread(&header_struct, sizeof(header_struct), 1, jcf->f) != 1) {
                return (-1);
        }
        /* Verify the magic number */
        magic = ntohl(header_struct.magic);
        if (magic != JCF_MAGIC) {
                return (-1);
        }
        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Finished processing Header.\n");
                fflush(stdout);
        }
        return (0);
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The JCF header must have already been read.
 *
 * Effects:
 *  Reads and stores the constant pool from the JCF.  Prints the
 *  dependencies if requested.  Returns 0 on success and -1 on failure.
 *  This function allocates memory that must be destroyed later, even if
 *  the function fails.
 */
static int
process_jcf_constant_pool(struct jcf_state *jcf)
{
        assert(jcf != NULL);
        assert(jcf->constant_pool.pool == NULL);
        uint16_t i;

        /* Read the constant pool count. */
        uint16_t constant_pool_count;
        if (fread(&constant_pool_count, sizeof(constant_pool_count),
            1, jcf->f) != 1)
                return (-1);
        /* store the constant pool count */
        constant_pool_count = ntohs(constant_pool_count);
        jcf->constant_pool.count = constant_pool_count;

        /* Allocate the constant pool. */
        /* 
         * The constant pool array contains one less entry than the count.
         * Also this is an abstract allocation, the info[0] allows for
         * variable length structures.
         */
        jcf->constant_pool.pool = 
            malloc(constant_pool_count * sizeof(struct jcf_cp_info *));

        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Finished allocating constant_pool.\n");
                fprintf(stdout, "Constant count is %hu.\n", 
                        jcf->constant_pool.count);
                fflush(stdout);
        }
        /* Read the constant pool. */
        for (i = 1; i < constant_pool_count; i++) { 

                /* Read the constant pool info tag */
                uint8_t tag;

                /* Read the constant pool info tag */
                if (fread(&tag, sizeof(tag), 1, jcf->f) != 1)
                        return (-1); 

                /* 
                 * Value of tag read. Now shift pointer back in order to read  
                 * the specific structure including tag. 
                 */
                if (fseek(jcf->f, -1, SEEK_CUR) != 0)
                        return (-1); 
                
                /* Process the rest of the constant info. */
                switch (tag) { 
                case JCF_CONSTANT_String:
                case JCF_CONSTANT_Class: { 

                        /* Read a constant that contains one u2. */
                        jcf->constant_pool.pool[i] = 
                            malloc(sizeof(struct jcf_cp_info_1u2));
                        struct jcf_cp_info_1u2 *u2_struct = 
                            (struct jcf_cp_info_1u2 *) jcf-> 
                                constant_pool.pool[i]; 
                        if (fread(u2_struct, sizeof(*u2_struct), 
                                1, jcf->f) != 1)
                                return (-1);
                        // u2_struct->tag = u2_struct->tag;
                        u2_struct->u2 = ntohs(u2_struct->u2);

                        /* testing purpose */
                        if (jcf->verbose_flag) {
                                fprintf(stdout, "Constant 1u2 at %d.\n", i);
                                fprintf(stdout, 
                                	"Allocated size %lu, read size %lu\n", 
                                    sizeof(struct jcf_cp_info_1u2), 
                                    sizeof(*u2_struct));
                                fflush(stdout);
                        }
                        break; 
                } 

                case JCF_CONSTANT_Fieldref:
                case JCF_CONSTANT_Methodref:
                case JCF_CONSTANT_InterfaceMethodref:
                case JCF_CONSTANT_NameAndType: { 

                        /* Read a constant that contains two u2's. */
                        jcf->constant_pool.pool[i] = 
                             malloc(sizeof(struct jcf_cp_info_2u2));
                        struct jcf_cp_info_2u2 *ref_struct = 
                           (struct jcf_cp_info_2u2 *) jcf-> 
                                constant_pool.pool[i];
                        if (fread(ref_struct, sizeof(*ref_struct), 
                                1, jcf->f) != 1)
                                return (-1);
                        // ref_struct->tag = ref_struct->tag;
                        ref_struct->body.u2_1 = 
                                ntohs(ref_struct->body.u2_1);
                        ref_struct->body.u2_2 = 
                                ntohs(ref_struct->body.u2_2);

                        /* testing purpose */
                        if (jcf->verbose_flag) {
                                fprintf(stdout, "Constant 2u2 at %d.\n", i);
                                fflush(stdout);
                        }
                        break; 
                } 

                case JCF_CONSTANT_Integer:
                case JCF_CONSTANT_Float: { 

                        /* Read a constant that contains one u4. */
                        jcf->constant_pool.pool[i] = 
                            malloc(sizeof(struct jcf_cp_info_1u4));
                        struct jcf_cp_info_1u4 *u4_struct = 
                           (struct jcf_cp_info_1u4 *) jcf->
                                constant_pool.pool[i];
                        if (fread(u4_struct, sizeof(*u4_struct), 
                                  1, jcf->f) != 1)
                                return (-1);
                        // u4_struct->tag = u4_struct->tag;
                        u4_struct->u4 = ntohl(u4_struct->u4);
                        if (jcf->verbose_flag) {
                                fprintf(stdout, "Constant 1u4 at %d.\n", i);
                                fflush(stdout);
                        }
                        break; 
                } 

                case JCF_CONSTANT_Long:
                case JCF_CONSTANT_Double: { 
                        /* 
                         * Read a constant that contains two u4's and
                         * occupies two indices in the constant pool. 
                         */
                        /* occupy two entries in the constant pool */
                        jcf->constant_pool.pool[i] = 
                            malloc(sizeof(struct jcf_cp_info_1u4));
                        jcf->constant_pool.pool[i + 1] = 
                            malloc(sizeof(struct jcf_cp_info_1u4));

                        struct jcf_cp_info_1u4 *info_high = 
                            (struct jcf_cp_info_1u4 *) jcf-> 
                                constant_pool.pool[i];
                        struct jcf_cp_info_1u4 *info_low = 
                            (struct jcf_cp_info_1u4 *) jcf->
                            constant_pool.pool[i + 1];
                        /* read tag, high_bytes, and low_bytes */
                        uint8_t tag;
                        uint32_t high_bytes;
                        uint32_t low_bytes;
                        if (fread(&tag, sizeof(tag), 1, jcf->f) != 1)
                                return (-1);
                        if (fread(&high_bytes, 4, 1, jcf->f) != 1)
                                return (-1);
                        if (fread(&low_bytes, 4, 1, jcf->f) != 1)
                                return (-1);

                        info_high->tag = tag;
                        info_low->tag = tag;
                        /* high_bytes after ntohl convertion is low */
                        info_low->u4 = ntohl(high_bytes);
                        /* low_bytes after ntohl convertion is high */
                        info_high->u4 = ntohl(low_bytes);
                        i += 1;

                        /* testing purpose */
                        if (jcf->verbose_flag) {
                                fprintf(stdout, 
                                	"Constant 2u4 at %d and %d.\n",
                                    i, i + 1);
                                fprintf(stdout, 
                                    "Allocated size %lu, actual read %lu.\n", 
                                    sizeof(struct jcf_cp_info_1u4), 
                                    sizeof(*info_high));
                                fflush(stdout);
                        }
                        break; 
                }        

                case JCF_CONSTANT_Utf8: { 

                        /* Read a UTF8 constant. */
                        uint8_t utf8_tag;
                        uint16_t utf8_length;
                        if (fread(&utf8_tag, 1, 1, jcf->f) != 1) 
                                return (-1);
                        if (fread(&utf8_length, 2, 1, jcf->f) != 1)
                                return (-1);
                        utf8_length = ntohs(utf8_length);
                        /* 
                         * note that 3 is sizeof(struct jcf_cp_utf8_info) 
                         * dismiss sizeof(byte[0]) 
                         */
                        jcf->constant_pool.pool[i] = malloc(3 + utf8_length);
                        struct jcf_cp_utf8_info *utf8_info = 
                            (struct jcf_cp_utf8_info *)jcf->constant_pool.
                            	pool[i]; 
                        utf8_info->tag = utf8_tag;
                        utf8_info->length = utf8_length; 

                        if (utf8_length != 0) {
                                if (fread(&utf8_info->bytes, utf8_length, 1, 
                                    jcf->f) != 1)
                                        return (-1);
                        }
                        /* testing purpose */
                        if (jcf->verbose_flag) {
                                fprintf(stdout, "Constant Utf8 at %d\n", i);
                                fprintf(stdout, 
                "Allocated size %d, actual size %lu. Length of bytes is %u.\n", 
                                    3 + utf8_length, sizeof(*utf8_info), 
                                    utf8_length);
                                fflush(stdout);
                        }
                        break;
                }
                default: 
                        return (-1);
                }
        }
        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Finished storing constant_pool.\n");
                fflush(stdout);
        }
        /* 
         * Print the dependencies if requested.  This must be done after
         * reading the entire pool because there are no guarantees about
         * constants not containing references to other constants after
         * them in the pool. 
         */
        for (i = 1; i < constant_pool_count; i++) {
                uint8_t tag = jcf->constant_pool.pool[i]->tag;
                /* 
                 * references are made by the CONSTANT Fieldref, 
                 * CONSTANT Methodref, and CONSTANT InterfaceMethodref 
                 * constants in the constant pool
                 */
                switch (tag) {
                case JCF_CONSTANT_Fieldref: {
                        if (jcf->depends_flag) {
                                printf("Dependency - ");
                                if (print_jcf_constant(jcf, 
                                    i, JCF_CONSTANT_Fieldref) != 0)
                                        return (-1);
                        printf("\n");
                        }
                        break;
                }
                case JCF_CONSTANT_Methodref: {
                        if (jcf->depends_flag) {
                                printf("Dependency - ");
                                if (print_jcf_constant(jcf, i, 
                                    JCF_CONSTANT_Methodref) != 0)
                                        return (-1);
                        printf("\n");
                        }
                        break;
                }
                case JCF_CONSTANT_InterfaceMethodref: {
                        if (jcf->depends_flag) {
                                printf("Dependency - ");
                                if (print_jcf_constant(jcf, i, 
                                    JCF_CONSTANT_InterfaceMethodref) != 0)
                                        return (-1);
                        printf("\n");
                        }
                        break;
                }
                default:
                        break;  /* Ignore all other constants. */
                }
        }
        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Process JCF constant_pool finished.\n");
                fflush(stdout);
        }
        return (0);
}

/*
 * Requires:
 *  The "pool" argument must be a valid JCF constant pool.
 *
 * Effects:
 *  Frees the memory allocated to store the constant pool.
 */
static void
destroy_jcf_constant_pool(struct jcf_constant_pool *pool)
{  

        assert(pool != NULL);
        assert(pool->pool != NULL);
        int i;
        /* free the memory allocated to each slot in the constant pool */
        uint16_t constant_pool_count = pool->count; 

        for (i = 1; i < constant_pool_count; i++) {
                free(pool->pool[i]);
        }
        /* free the memory allocated to the constant pool as a whole */
        free(pool->pool);

        /* testing purpose */
        /*
        if (jcf->verbose_flag) {
                fprintf(stdout, "Constant pool memeory freed.\n");
                fflush(stdout);
        }
        */
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The JCF header and constant pool must have
 *  already been read.
 *
 * Effects:
 *  Reads and the Java class file body from file "jcf.f".  Returns 0 on
 *  success.
 */
static int
process_jcf_body(struct jcf_state *jcf)
{
        assert(jcf != NULL);
        struct jcf_body body_struct;

        /* Read the body. */
        if (fread(&body_struct, sizeof(body_struct), 1, jcf->f) != 1) {
                readjcf_error();
                return (-1);
        }

        /* Verifies whether the bytes are valid */
        if (jcf->verbose_flag){
                fprintf(stdout, "JCF body process finished.\n");
                fflush(stdout);
        }
        return (0);
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The JCF header, constant pool, and body must
 *  have already been read.
 *
 * Effects:
 *  Reads and the Java class file interfaces from file "jcf.f". Returns
 *  0 on success.
 */
static int
process_jcf_interfaces(struct jcf_state *jcf)
{
        assert(jcf != NULL);
        int i;

        /* Read the interfaces count. */
        int16_t count;
        if (fread(&count, sizeof(count), 1, jcf->f) != 1)
                return (-1);
        count = ntohs(count); 

        /* Read the interfaces. */
        for (i = 0; i < count; i++) {
                uint16_t interface_index; 
                if (fread(&interface_index, 2, 1, jcf->f) != 1) {
                        readjcf_error(); 
                        return (-1);
                }
        } 
        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Process JCF interfaces finished.\n");
                fflush(stdout);
        }
        return (0);
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The JCF header, constant pool, body, and
 *  interfaces must have already been read.  The JCF constant pool in
 *  "jcf" must be initialized.
 *
 * Effects:
 *  Reads and the Java class file fields from file "jcf.f".  Prints the
 *  exported fields, if requested.  Returns 0 on success and -1 on
 *  failure.
 */
static int
process_jcf_fields(struct jcf_state *jcf)
{
        return (process_jcf_fields_and_methods_helper(jcf));
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The JCF header, constant pool, body,
 *  interfaces, and fields must have already been read.  The JCF
 *  constant pool in "jcf" must be initialized.
 *
 * Effects:
 *  Reads and the Java class file fields from file "jcf.f".  Prints the
 *  exported methods, if requested.  Returns 0 on success and -1 on
 *  failure.
 */
static int
process_jcf_methods(struct jcf_state *jcf)
{
        return (process_jcf_fields_and_methods_helper(jcf));
}

/*
 * Requires:
 *  All the requirements of either process_jcf_fields or
 *  process_jcf_methods.
 *
 * Effects:
 *  Reads and the Java class file fields or methods from file "jcf.f".
 *  Prints the exports, if requested. Returns 0 on success and -1 on
 *  failure.
 */
static int
process_jcf_fields_and_methods_helper(struct jcf_state *jcf)
{
        uint16_t count;
        struct jcf_field_info info;
        int i;

        assert(jcf != NULL);

        /* Read the count. */
        if (fread(&count, sizeof(count), 1, jcf->f) != 1)
                return (-1);
        count = ntohs(count);

        /* Read the methods */
        for (i = 0; i < count; i++) {
                /* Read the info. */
                if (fread(&info, sizeof(info), 1, jcf->f) != 1)
                        return (-1);
                info.access_flags = ntohs(info.access_flags);
                info.name_index = ntohs(info.name_index);
                info.descriptor_index = ntohs(info.descriptor_index);

                /* Print the info. */
                if (jcf->exports_flag &&
                    info.access_flags & JCF_ACC_PUBLIC) {
                        printf("Export - ");  
                        if (print_jcf_constant(jcf, info.name_index,
                            JCF_CONSTANT_Utf8) != 0)
                                return (-1);
                        printf(" "); 

                        if (print_jcf_constant(jcf, info.descriptor_index,
                            JCF_CONSTANT_Utf8) != 0)
                                return (-1); 

                        printf("\n");
                }

                /* Read the attributes. */
                if (process_jcf_attributes(jcf) != 0)
                        return (-1);
        }

        return (0);
}

/*
 * Requires:
 *  The "jcf" argument must be a valid struct jcf_state.  "jcf.f" must
 *  be a valid open file.  The next part of the JCF to be read must be
 *  an attributes count, followed by an array of attributes exactly
 *  count long.
 *
 * Effects:
 *  Reads the attributes.  Returns 0 on success and -1 on
 *  failure.
 */
static int
process_jcf_attributes(struct jcf_state *jcf)
{  

        assert(jcf != NULL);
        int i;
        uint16_t attributes_count;
        uint16_t attribute_name_index;
        uint32_t attribute_length;

        /* Read the attributes count. */
        if (fread(&attributes_count, 2, 1, jcf->f) != 1)
                return (-1);
        attributes_count = ntohs(attributes_count);

        /* Read the attributes */
        for (i = 0; i < attributes_count; i++) {
                /* Read the attribute name index. */
                if (fread(&attribute_name_index, 2, 1, jcf->f) != 1) {
                        readjcf_error();
                        return (-1);
                }
                // attribute_name_index = ntohs(attribute_name_index);
                /* Read the attribute length. */
                if (fread(&attribute_length, 4, 1, jcf->f) != 1) {
                        readjcf_error();
                        return (-1);
                }
                attribute_length = ntohl(attribute_length);
                /* Read the attribute data. */
                uint8_t info[attribute_length];
                if (fread(&info, sizeof(info), 1, jcf->f) != 1) {
                        readjcf_error();
                        return (-1);
                }
        }
        /* testing purpose */
        if (jcf->verbose_flag) {
                fprintf(stdout, "Process JCF attributes finished.\n");
                fflush(stdout);
        }
        return (0);
}

/* 
 * Requires:
 *   Nothing.
 *
 * Effects:
 *   Reads the Java class file and performs pass 1 verification.  Also
 *   prints the class' dependencies and exports, if requested.
 */
int
main(int argc, char **argv)
{
        /* Structure for holding all processing state */
        struct jcf_state jcf;

        /* Option character */
        int c;

        /* Error return - was there an error during processing? */
        int err;

        /* Option index */
        extern int optind;

        /* Abort flag - was there an error on the command line? */ 
        bool abort_flag = false;

        /* Option flags - were these options on the command line? */
        bool depends_flag = false;
        bool exports_flag = false;
        bool verbose_flag = false;

        /* Process the command line arguments. */
        while ((c = getopt(argc, argv, "dev")) != -1) {
                switch (c) {
                case 'd':
                        /* Print depends. */
                        if (depends_flag) {
                                /* Flags can only appear once. */
                                abort_flag = true;
                        } else {
                                depends_flag = true;
                        }
                        break;
                case 'e':
                        /* Print exports. */
                        if (exports_flag) {
                                /* Flags can only appear once. */
                                abort_flag = true;
                        } else {
                                exports_flag = true;
                        }
                        break;
                case 'v':
                        /* Be verbose. */
                        if (verbose_flag) {
                                /* Flags can only appear once. */
                                abort_flag = true;
                        } else {
                                verbose_flag = true;
                        }
                        break;
                case '?':
                        /* Error character returned by getopt. */
                        abort_flag = true;
                }
        }
        if (abort_flag || optind == argc || argc > optind + 1) {
                fprintf(stderr, "usage: %s [-d] [-e] [-v] <input filename>\n",
                    argv[0]);
                return (1); /* Indicate an error. */
        }

        /* Initialize the jcf_state structure. */
        jcf.f = NULL;
        jcf.depends_flag = depends_flag;
        jcf.exports_flag = exports_flag;
        jcf.verbose_flag = verbose_flag;
        jcf.constant_pool.count = 0;
        jcf.constant_pool.pool = NULL;

        /* Open the class file. */
        jcf.f = fopen(argv[optind], "r");
        if (jcf.f == NULL) {
                readjcf_error();
                return (1);
        }

        /* Process the JCF header. */
        err = process_jcf_header(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF constant pool. */
        err = process_jcf_constant_pool(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF body. */
        err = process_jcf_body(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF interfaces. */
        err = process_jcf_interfaces(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF fields. */
        err = process_jcf_fields(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF methods. */
        err = process_jcf_methods(&jcf);
        if (err != 0)
                goto failed;

        /* Process the JCF final attributes. */
        err = process_jcf_attributes(&jcf);
        if (err != 0)
                goto failed;

        /* Check for extra data. */
        if (fgetc(jcf.f) != EOF) {
                err = -1;
                goto failed;
        }

failed:
        if (jcf.constant_pool.pool != NULL)
                destroy_jcf_constant_pool(&jcf.constant_pool);
        fclose(jcf.f);
        if (err != 0)
                readjcf_error();
        return (err);
}

/*
 * The last lines of this file configure the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
