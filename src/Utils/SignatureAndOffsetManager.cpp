#include "SignatureAndOffsetManager.hpp"
#include "Memory.hpp"
#include "Logger.hpp"

SignatureAndOffsetManager Mgr;

void SignatureAndOffsetManager::addSignature(unsigned int hash, const char* sig, const char* name) {
    sigs[hash] = { sig ? sig : "", name ? name : "", 0 };
}

void SignatureAndOffsetManager::removeSignature(unsigned int hash) {
    sigs.erase(hash);
}

const char* SignatureAndOffsetManager::getSig(unsigned int hash) const {
    auto it = sigs.find(hash);
    return it != sigs.end() ? it->second.signature.c_str() : nullptr;
}

const char* SignatureAndOffsetManager::getSigName(unsigned int hash) const {
    auto it = sigs.find(hash);
    return it != sigs.end() ? it->second.name.c_str() : nullptr;
}

uintptr_t SignatureAndOffsetManager::getSigAddress(unsigned int hash) const {
    auto it = sigs.find(hash);
    return it != sigs.end() ? it->second.address : 0;
}

void SignatureAndOffsetManager::addOffset(unsigned int hash, int offset) {
    offsets[hash] = offset;
}

int SignatureAndOffsetManager::getOffset(unsigned int hash) const {
    auto it = offsets.find(hash);
    return it != offsets.end() ? it->second : 0;
}

void SignatureAndOffsetManager::clear() {
    sigs.clear();
    offsets.clear();
}

void SignatureAndOffsetManager::scanAllSignatures() {
    Logger::info("Scanning verified 1.26.x signatures...", "SigManager");
    for (auto& [hash, data] : sigs) {
        data.address = Memory::findSig(data.signature, data.name);
    }
    Logger::info("Signature scanning finished.", "SigManager");
}
