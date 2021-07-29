#include "encrypt.h"

class NoneDecEncrypter final : public BaseDecEncrypter {
public:
    void encrypt(char *dst, std::size_t dlen, char *src,
                 std::size_t slen) override {
        memmove(dst, src, slen);
    }
    void decrypt(char *dst, std::size_t dlen, char *src,
                 std::size_t slen) override {
        memmove(dst, src, slen);
    }
};

std::unique_ptr<BaseDecEncrypter> getDecEncrypter() {
    return std::move(my_make_unique<NoneDecEncrypter>());
}
