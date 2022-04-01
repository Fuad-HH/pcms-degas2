#include <Omega_h_file.hpp>
#include <Omega_h_library.hpp>
#include <Omega_h_array_ops.hpp>
#include <Omega_h_comm.hpp>
#include <Omega_h_mesh.hpp>
#include <Omega_h_for.hpp>
#include <redev_comm.h>
#include "wdmcpl.h"
#include "test_support.h"

namespace ts = test_support;

void prepareMsg(Omega_h::Mesh& mesh, redev::ClassPtn& ptn,
    ts::OutMsg& out, redev::LOs& permute) {
  //transfer vtx classification to host
  auto classIds = mesh.get_array<Omega_h::ClassId>(0, "class_id");
  auto classIds_h = Omega_h::HostRead(classIds);
  //count number of vertices going to each destination process by calling getRank - degree array
  std::map<int,int> destRankCounts;
  const auto ptnRanks = ptn.GetRanks();
  for(auto rank : ptnRanks) {
    destRankCounts[rank] = 0;
  }
  for(auto i=0; i<classIds_h.size(); i++) {
    auto dr = ptn.GetRank(classIds_h[i]);
    assert(destRankCounts.count(dr));
    destRankCounts[dr]++;
  }
  REDEV_ALWAYS_ASSERT(destRankCounts[0] == 6);
  REDEV_ALWAYS_ASSERT(destRankCounts[1] == 13);
  //create dest and offsets arrays from degree array
  out.offset.resize(destRankCounts.size()+1);
  out.dest.resize(destRankCounts.size());
  out.offset[0] = 0;
  int i = 1;
  for(auto rankCount : destRankCounts) {
    out.dest[i-1] = rankCount.first;
    out.offset[i] = out.offset[i-1]+rankCount.second;
    i++;
  }
  redev::LOs expectedDest = {0,1};
  REDEV_ALWAYS_ASSERT(out.dest == expectedDest);
  redev::LOs expectedOffset = {0,6,19};
  REDEV_ALWAYS_ASSERT(out.offset == expectedOffset);
  //fill permutation array such that for vertex i permute[i] contains the
  //  position of vertex i's data in the message array
  std::map<int,int> destRankIdx;
  for(size_t i=0; i<out.dest.size(); i++) {
    auto dr = out.dest[i];
    destRankIdx[dr] = out.offset[i];
  }
  auto gids = mesh.globals(0);
  auto gids_h = Omega_h::HostRead(gids);
  permute.resize(classIds_h.size());
  for(auto i=0; i<classIds_h.size(); i++) {
    auto dr = ptn.GetRank(classIds_h[i]);
    auto idx = destRankIdx[dr]++;
    permute[i] = idx;
  }
  redev::LOs expectedPermute = {0,6,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18};
  REDEV_ALWAYS_ASSERT(permute == expectedPermute);
}

//creates rdvPermute given inGids and the rdv mesh instance
//this only needs to be computed once for each topological dimension
//TODO - port to GPU
void getRdvPermutation(Omega_h::Mesh& mesh, redev::GOs& inGids, redev::GOs& rdvPermute) {
  auto gids = mesh.globals(0);
  auto gids_h = Omega_h::HostRead(gids);
  typedef std::map<Omega_h::GO, int> G2I;
  G2I in2idx;
  for(size_t i=0; i<inGids.size(); i++)
    in2idx[inGids[i]] = i;
  G2I gid2idx;
  for(int i=0; i<gids_h.size(); i++)
    gid2idx[gids_h[i]] = i;
  rdvPermute.resize(inGids.size());
  auto gidIter = gid2idx.begin();
  for(auto inIter=in2idx.begin(); inIter != in2idx.end(); inIter++) {
    while(gidIter->first != inIter->first)
      gidIter++;
    //must have been found
    REDEV_ALWAYS_ASSERT(gidIter != gid2idx.end());
    REDEV_ALWAYS_ASSERT(gidIter->first == inIter->first);
    //store permutation
    const auto gidIdx = gidIter->second;
    const auto inIdx = inIter->second;
    REDEV_ALWAYS_ASSERT(gids_h[gidIdx] == inGids[inIdx]);
    REDEV_ALWAYS_ASSERT(static_cast<size_t>(inIdx) < inGids.size());
    rdvPermute[inIdx] = gidIdx;
  }
}

int main(int argc, char** argv) {
  auto lib = Omega_h::Library(&argc, &argv);
  auto world = lib.world();
  int rank = world->rank();
  if(argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <1=isRendezvousApp,0=isParticipant> /path/to/omega_h/mesh\n";
    std::cerr << "WARNING: this test is currently hardcoded for the xgc1_data/Cyclone_ITG/Cyclone_ITG_deltaf_23mesh mesh\n";
    exit(EXIT_FAILURE);
  }
  OMEGA_H_CHECK(argc == 3);
  auto isRdv = atoi(argv[1]);
  Omega_h::Mesh mesh(&lib);
  Omega_h::binary::read(argv[2], lib.world(), &mesh);
  if(!rank) REDEV_ALWAYS_ASSERT(mesh.nelems() == 23); //sanity check that the loaded mesh is the expected one
  redev::LOs ranks;
  redev::LOs classIds;
  if(isRdv) {
    //partition the omegah mesh by classification and return the
    //rank-to-classid array
    ts::getClassPtn(mesh, ranks, classIds);
    REDEV_ALWAYS_ASSERT(ranks.size()==3);
    REDEV_ALWAYS_ASSERT(ranks.size()==classIds.size());
    Omega_h::vtk::write_parallel("rdvSplit.vtk", &mesh, mesh.dim());
  }
  auto ptn = redev::ClassPtn(ranks,classIds);
  redev::Redev rdv(MPI_COMM_WORLD,ptn,isRdv);
  rdv.Setup();
  const std::string name = "meshVtxIds";
  const int rdvRanks = 2;
  redev::AdiosComm<redev::GO> comm(MPI_COMM_WORLD, rdvRanks, rdv.getToEngine(), rdv.getToIO(), name);

  redev::LOs appOutPermute;
  ts::OutMsg appOut;

  redev::GOs rdvInPermute;
  ts::InMsg rdvIn;

  for(int iter=0; iter<3; iter++) {
    if(!rank) fprintf(stderr, "isRdv %d iter %d\n", isRdv, iter);
    MPI_Barrier(MPI_COMM_WORLD);
    //////////////////////////////////////////////////////
    //the non-rendezvous app sends global vtx ids to rendezvous
    //////////////////////////////////////////////////////
    if(!isRdv) {
      //build dest and offsets arrays
      if(iter==0) prepareMsg(mesh, ptn, appOut, appOutPermute);
      //fill message array
      auto gids = mesh.globals(0);
      auto gids_h = Omega_h::HostRead(gids);
      redev::GOs msgs(gids_h.size(),0);
      for(size_t i=0; i<msgs.size(); i++) {
        msgs[appOutPermute[i]] = gids_h[i];
      }
      auto start = std::chrono::steady_clock::now();
      comm.Pack(appOut.dest, appOut.offset, msgs.data());
      comm.Send();
      ts::getAndPrintTime(start,name + " write",rank);
    } else {
      auto start = std::chrono::steady_clock::now();
      const bool knownSizes = (iter == 0) ? false : true;
      ts::unpack(comm,knownSizes,rdvIn);
      REDEV_ALWAYS_ASSERT(rdvIn.offset == redev::GOs({0,6,19}));
      REDEV_ALWAYS_ASSERT(rdvIn.srcRanks == redev::GOs({0,0}));
      if(!rank) {
        REDEV_ALWAYS_ASSERT(rdvIn.start==0 && rdvIn.count==6);
        REDEV_ALWAYS_ASSERT(rdvIn.msgs == redev::GOs({0,2,3,4,5,6}));
      } else {
        REDEV_ALWAYS_ASSERT(rdvIn.start==6 && rdvIn.count==13);
        REDEV_ALWAYS_ASSERT(rdvIn.msgs == redev::GOs({1,7,8,9,10,11,12,13,14,15,16,17,18}));
      }
      ts::getAndPrintTime(start,name + " read",rank);
      //attach the ids to the mesh
      if(iter==0) getRdvPermutation(mesh, rdvIn.msgs, rdvInPermute);
      ts::checkAndAttachIds(mesh, "inVtxGids", rdvIn.msgs, rdvInPermute);
      Omega_h::vtk::write_parallel("rdvInGids.vtk", &mesh, mesh.dim());
    } //end non-rdv -> rdv
  } //end iter loop
  return 0;
}
