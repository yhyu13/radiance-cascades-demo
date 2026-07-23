#include "reference_merge_validation.h"
#include "config.h"
#include "gl_helpers.h"
#include "reference_transport.h"
#include "raylib.h"
#include <GL/glew.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>

namespace {
#include "reference_merge_golden.inc"
struct alignas(16) Req{float uvLocal[4],localB[4],alphas[4],colors[4][4];};
struct alignas(16) Rec{float merged[4],upper[4],meta[4];};
static_assert(sizeof(Req)==112&&sizeof(Rec)==48);
struct Result{int checked=0,cpuFail=0,gpuFail=0,glErrors=0;float maxErr=0;bool shader=false;bool pass()const{return checked==5&&cpuFail==0&&gpuFail==0&&glErrors==0&&shader;}};
bool near(float a,double b){return std::abs(a-(float)b)<2e-4f;}
void errors(Result&r){for(GLenum e=glGetError();e!=GL_NO_ERROR;e=glGetError())r.glErrors++;}
bool write(const std::string&p,const Result&r){std::filesystem::path q(p);std::error_code ec;if(q.has_parent_path())std::filesystem::create_directories(q.parent_path(),ec);std::ofstream o(p);if(ec||!o)return false;o<<"{\n  \"schema_version\": \"reference-upper-merge-report-v1\",\n  \"result\": \""<<(r.pass()?"PASS":"FAIL")<<"\",\n  \"gates\": {\"G6-upper-cascade-weighted-merge\": \""<<(r.pass()?"PASS":"FAIL")<<"\"},\n  \"metrics\": {\"fixtures_checked\": "<<r.checked<<", \"cpu_failures\": "<<r.cpuFail<<", \"gpu_failures\": "<<r.gpuFail<<", \"gl_errors\": "<<r.glErrors<<", \"max_error\": "<<r.maxErr<<"},\n  \"scope\": {\"temporal_feedback\": \"disabled\", \"atlas_swap\": \"disabled\", \"final_rendering\": \"disabled\"}\n}\n";return o.good()&&r.pass();}
}
bool runReferenceMergeValidation(const std::string& path){Result r;std::vector<Req> qs;for(const auto&f:kGoldenMerge){Req q{};q.uvLocal[0]=f.uvx;q.uvLocal[1]=f.uvy;q.uvLocal[2]=f.lr;q.uvLocal[3]=f.lg;q.localB[0]=f.lb;q.localB[1]=f.distance;q.localB[2]=f.alpha;
 // Fixture colors/alphas are reloaded from the checked golden JSON pattern.
 // Names define the controlled visibility masks used by the generator.
 float a[4]={-1,-1,-1,-1};float c[4][3]={{.2f,.4f,.6f},{.2f,.4f,.6f},{.2f,.4f,.6f},{.2f,.4f,.6f}};
 std::string n=f.name;if(n.find("one_visible")!=std::string::npos){a[1]=a[2]=a[3]=-.4f;for(auto&v:c){v[0]=1;v[1]=v[2]=0;}}if(n.find("none_visible")!=std::string::npos){for(float&x:a)x=-.4f;for(auto&v:c)v[0]=v[1]=v[2]=9;}if(n.find("edge")!=std::string::npos)for(auto&v:c){v[0]=1;v[1]=2;v[2]=3;}if(n.find("interior")!=std::string::npos){float aa[4]={-1,-.4f,-1,-.4f};for(int i=0;i<4;i++){a[i]=aa[i];c[i][0]=.2f;c[i][1]=.1f;c[i][2]=.3f;}}
 for(int i=0;i<4;i++){q.alphas[i]=a[i];for(int j=0;j<3;j++)q.colors[i][j]=c[i][j];}qs.push_back(q);r.checked++;}
 // CPU oracle with synthetic physical atlas keyed from candidate address sets.
 for(size_t k=0;k<qs.size();k++){const auto&f=kGoldenMerge[k];const Req&q=qs[k];auto prelim=reftransport::mergeUpper({f.uvx,f.uvy},{glm::vec3(f.lr,f.lg,f.lb),(float)f.alpha},f.distance,[](glm::ivec2){return glm::vec4(0);});auto fetch=[&](glm::ivec2 p){glm::vec4 v(0);for(int i=0;i<4;i++){auto&c=prelim.candidates[i];if(p==glm::ivec2(c.lookBackPhysical))v.a=q.alphas[i];for(int j=0;j<4;j++)if(p==glm::ivec2(c.radiancePhysical[j]))v=glm::vec4(q.colors[i][0],q.colors[i][1],q.colors[i][2],v.a);}return v;};auto x=reftransport::mergeUpper({f.uvx,f.uvy},{glm::vec3(f.lr,f.lg,f.lb),(float)f.alpha},f.distance,fetch);float e=std::max({std::abs(x.mergedRgb.x-(float)f.mr),std::abs(x.mergedRgb.y-(float)f.mg),std::abs(x.mergedRgb.z-(float)f.mb)});r.maxErr=std::max(r.maxErr,e);if(e>2e-4f||!near(x.upperRgb.x,f.ur)||!near(x.visibleWeight,f.visibleWeight))r.cpuFail++;}
 InitWindow(64,64,"phase5-merge");glewExperimental=GL_TRUE;glewInit();while(glGetError()!=GL_NO_ERROR){}gl::setShaderRoot(RC3D_SHADER_ROOT);gl::clearShaderSourceRecords();GLuint prog=gl::loadComputeShader(gl::resolveShaderPath("reference_merge_synthetic.comp"),"reference_merge_synthetic.comp");r.shader=prog!=0;GLuint a=0,b=0;std::vector<Rec> out(qs.size());if(prog){glGenBuffers(1,&a);glBindBuffer(GL_SHADER_STORAGE_BUFFER,a);glBufferData(GL_SHADER_STORAGE_BUFFER,qs.size()*sizeof(Req),qs.data(),GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,a);glGenBuffers(1,&b);glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);glBufferData(GL_SHADER_STORAGE_BUFFER,out.size()*sizeof(Rec),nullptr,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,b);glUseProgram(prog);glUniform1i(glGetUniformLocation(prog,"uCount"),(int)qs.size());glDispatchCompute(1,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,out.size()*sizeof(Rec),out.data());errors(r);for(size_t i=0;i<out.size();i++){auto&f=kGoldenMerge[i];float e=std::max({std::abs(out[i].merged[0]-(float)f.mr),std::abs(out[i].merged[1]-(float)f.mg),std::abs(out[i].merged[2]-(float)f.mb)});r.maxErr=std::max(r.maxErr,e);if(e>2e-4f||!near(out[i].upper[0],f.ur)||!near(out[i].upper[3],f.visibleWeight)||!near(out[i].merged[3],f.alpha))r.gpuFail++;}}
 if(b)glDeleteBuffers(1,&b);if(a)glDeleteBuffers(1,&a);if(prog)glDeleteProgram(prog);bool ok=write(path,r);CloseWindow();std::cout<<"[PHASE5] merge report="<<path<<" result="<<(ok?"PASS":"FAIL")<<"\n";return ok;}
