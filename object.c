// object.c — Content-addressable object store
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // Build header
    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    header_len += 1; // include null byte

    // Build full object = header + \0 + data
    size_t full_len = header_len + len;
    uint8_t *full = malloc(full_len);
    if (!full) return -1;
    memcpy(full, header, header_len);
    memcpy(full + header_len, data, len);

    // Compute hash
    compute_hash(full, full_len, id_out);

    // Deduplication
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // Create shard directory
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    // Write to temp file
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/%.2s/tmp_XXXXXX", OBJECTS_DIR, hex);
    int fd = mkstemp(tmp_path);
    if (fd < 0) { free(full); return -1; }

    write(fd, full, full_len);
    fsync(fd);
    close(fd);
    free(full);

    // Rename to final path
    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));
    if (rename(tmp_path, final_path) < 0) return -1;

    // fsync the directory
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) { fsync(dir_fd); close(dir_fd); }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(file_len);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, file_len, f);
    fclose(f);

    // Verify integrity
    ObjectID computed;
    compute_hash(buf, file_len, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf); return -1;
    }

    // Parse header
    uint8_t *null_pos = memchr(buf, '\0', file_len);
    if (!null_pos) { free(buf); return -1; }

    if (strncmp((char *)buf, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp((char *)buf, "tree", 4) == 0) *type_out = OBJ_TREE;
    else if (strncmp((char *)buf, "commit", 6) == 0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }

    size_t header_len = null_pos - buf + 1;
    size_t data_len = file_len - header_len;

    void *out = malloc(data_len);
    if (!out) { free(buf); return -1; }
    memcpy(out, buf + header_len, data_len);

    *data_out = out;
    *len_out = data_len;
    free(buf);
    return 0;
}
