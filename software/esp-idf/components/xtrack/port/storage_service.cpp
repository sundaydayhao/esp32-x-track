#include "Utils/StorageService/StorageService.h"
#include "xtrack_board.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <limits>

StorageService::StorageService(const char* path,uint32_t size):FilePath(path),BufferSize(size){}
StorageService::~StorageService(){for(auto* node:NodePool)delete node;}
StorageService::Node_t* StorageService::SearchNode(const char* key){for(auto* node:NodePool)if(!std::strcmp(key,node->key))return node;return nullptr;}
bool StorageService::Add(const char* key,void* value,uint16_t size,DataType_t type) {
    if(!key || !value || !size || SearchNode(key))return false;
    NodePool.push_back(new Node_t{key,value,size,type});return true;
}
bool StorageService::Remove(const char* key) {
    auto* node=SearchNode(key);if(!node)return false;
    NodePool.erase(std::remove(NodePool.begin(),NodePool.end(),node),NodePool.end());delete node;return true;
}
bool StorageService::LoadFile() {
    // SD reinsertion must not roll the running trip back to an older saved value.
    static bool loaded=false;
    auto read_json=[this](const std::string& name)->cJSON* {
        FILE* file=std::fopen(name.c_str(),"rb");if(!file)return nullptr;
        std::string data(BufferSize+1,'\0');size_t n=std::fread(data.data(),1,data.size(),file);bool io=std::ferror(file);std::fclose(file);
        if(io || n>BufferSize)return nullptr;
        data.resize(n);auto* json=cJSON_Parse(data.c_str());
        if(cJSON_IsObject(json))return json;
        cJSON_Delete(json);return nullptr;
    };
    auto apply=[this](cJSON* json,bool import) {for(auto* node:NodePool) {
        if(import && std::strncmp(node->key,"sysConfig.",10))continue;
        auto* item=cJSON_GetObjectItemCaseSensitive(json,node->key);if(!item)continue;
        if(node->type==TYPE_STRING && cJSON_IsString(item)) {
            if(std::strlen(item->valuestring)>=node->size)continue;
            if(!std::strcmp(node->key,"sysConfig.mapExtName") && std::strcmp(item->valuestring,"bin") && std::strcmp(item->valuestring,"png"))continue;
            if(!std::strcmp(node->key,"sysConfig.mapDirPath") && (item->valuestring[0]!='/' || xtrack::storage_path(item->valuestring).empty()))continue;
            std::snprintf(static_cast<char*>(node->value),node->size,"%s",item->valuestring);
        } else if((cJSON_IsNumber(item) || cJSON_IsBool(item)) && std::isfinite(item->valuedouble)) {
            double number=cJSON_IsBool(item)?double(cJSON_IsTrue(item)):item->valuedouble;
            if(!std::strcmp(node->key,"sysConfig.latitude") && std::abs(number)>85.05112878)continue;
            if(!std::strcmp(node->key,"sysConfig.longitude") && std::abs(number)>180)continue;
            if(node->type==TYPE_INT) {
                if(std::floor(number)!=number)continue;
                if(!std::strcmp(node->key,"sysConfig.timeZone")) {
                    if(number < -12 || number > 14)continue;
                    int16_t v=int16_t(number);std::memcpy(node->value,&v,sizeof(v));
                } else {
                    if(number<0 || number>UINT32_MAX || (node->size==sizeof(bool) && number>1))continue;
                    uint32_t v=uint32_t(number);std::memcpy(node->value,&v,std::min<size_t>(node->size,sizeof(v)));
                }
            }
            else if(node->type==TYPE_FLOAT && node->size==sizeof(float) && std::abs(item->valuedouble)<=std::numeric_limits<float>::max()){float v=item->valuedouble;std::memcpy(node->value,&v,sizeof(v));}
            else if(node->type==TYPE_DOUBLE && node->size==sizeof(double))std::memcpy(node->value,&item->valuedouble,sizeof(double));
        }
    }};
    if(!loaded) {
        for(const char* path:{FilePath,"/.SystemSaveBackup.json"}) {
            auto* json=read_json(xtrack::storage_path(path));if(!json)continue;
            apply(json,false);cJSON_Delete(json);break;
        }
        loaded=true;
    }
    if(xtrack::storage_ready()) {
        auto* json=read_json("/sdcard/XTrackConfig.json");
        if(json){apply(json,true);cJSON_Delete(json);}
    }
    return true; // Defaults are a valid configuration on first boot.
}
bool StorageService::SaveFile(const char* backup) {
    cJSON* json=cJSON_CreateObject();if(!json)return false;
    for(auto* node:NodePool) {
        if(node->type==TYPE_STRING)cJSON_AddStringToObject(json,node->key,static_cast<const char*>(node->value));
        else {
            double value=0;
            if(node->type==TYPE_INT){
                if(!std::strcmp(node->key,"sysConfig.timeZone")){int16_t v=0;std::memcpy(&v,node->value,sizeof(v));value=v;}
                else {uint32_t v=0;std::memcpy(&v,node->value,std::min<size_t>(node->size,sizeof(v)));value=v;}
            }
            else if(node->type==TYPE_FLOAT){float v=0;std::memcpy(&v,node->value,std::min<size_t>(node->size,sizeof(v)));value=v;}
            else if(node->type==TYPE_DOUBLE)std::memcpy(&value,node->value,std::min<size_t>(node->size,sizeof(value)));
            if(std::isfinite(value))cJSON_AddNumberToObject(json,node->key,value);
        }
    }
    char* data=cJSON_PrintUnformatted(json);cJSON_Delete(json);if(!data)return false;
    auto path=xtrack::storage_path(backup?backup:FilePath);auto tmp=path+".tmp";
    FILE* f=std::fopen(tmp.c_str(),"wb");bool ok=false;
    if(f){size_t n=std::strlen(data);ok=std::fwrite(data,1,n,f)==n && std::fflush(f)==0 && fsync(fileno(f))==0;if(std::fclose(f))ok=false;}
    cJSON_free(data);
    // ESP VFS/FatFs rename may require removing the old destination. The independent
    // backup remains available if power fails between unlink and rename.
    if(ok){std::remove(path.c_str());ok=std::rename(tmp.c_str(),path.c_str())==0;}
    return ok;
}
