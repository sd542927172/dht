#ifndef _DHC_H_
#define _DHC_H_

#include <dbfe.h>
#include <arpc.h>
#include <chord.h>
#include <chord_types.h>
#include <dhash_types.h>
#include <dhc_prot.h>

extern void set_locations (vec<ptr<location> >, ptr<vnode>, vec<chordID>);

// PK blocks data structure for maintaining consistency.

struct replica_t {
  u_int64_t seqnum;
  vec<chordID> nodes;
  uint size;
  u_char *buf;
  
  replica_t () : seqnum (0), size (0), buf (NULL) { };

  replica_t (char *bytes, uint sz) : buf (NULL)
  {
    size = sizeof (size) + sizeof (seqnum);
    bcopy (bytes + sizeof (size), &seqnum, sizeof (seqnum));
    if (sz-size < 0) {
      warn << "Fatal replica_t: Negative size\n";
      exit (-1);
    }
    //No idea if this is right.
    nodes.setsize (sz-size);
    bcopy (bytes + sizeof (size) + sizeof (seqnum), nodes.base (), sz-size);
  }

  ~replica_t () { if (buf) free (buf); nodes.clear (); }

  u_char *bytes ()
  {
    if (buf) free (buf);
    size = sizeof (uint) + sizeof (seqnum) + nodes.size ();
    buf = (u_char *) malloc (size);
    bcopy (&size, buf, sizeof (uint));
    bcopy (&seqnum, buf + sizeof (uint), sizeof (seqnum));
    bcopy (nodes.base (), buf + sizeof (uint) + sizeof (seqnum), nodes.size ());
    return buf;
  }
};

struct keyhash_meta {
  replica_t config;
  paxos_seqnum_t accepted;
  uint size;
  u_char *buf;
  
  keyhash_meta () : size (0), buf (NULL)
  {
    accepted.seqnum = 0;
    bzero (&accepted.proposer, sizeof (chordID));        
  }

  keyhash_meta (char *bytes, uint sz) : size (sz), buf (NULL)
  {
    uint csize;
    bcopy (bytes + sizeof (uint), &csize, sizeof (uint));
    config = replica_t (bytes + sizeof (uint), csize);
    bcopy (bytes + sizeof (uint) + csize, &accepted.seqnum, sizeof (u_int64_t));
    bcopy (bytes + sizeof (uint) + csize + sizeof (u_int64_t),
	   &accepted.proposer, sizeof (chordID));
  }

  ~keyhash_meta () { if (buf) free (buf); }

  u_char *bytes ()
  {
    if (buf) free (buf);
    u_char *cbuf = config.bytes ();
    size = sizeof (uint) + config.size + sizeof (u_int64_t) + sizeof (chordID);
    buf = (u_char *) malloc (size);
    bcopy (&size, buf, sizeof (uint));
    bcopy (cbuf, buf + sizeof (uint), config.size);
    bcopy (&accepted.seqnum, buf + sizeof (uint) + config.size, sizeof (u_int64_t));
    bcopy (&accepted.proposer, buf + sizeof (uint) + config.size + 
	   sizeof (u_int64_t), sizeof (chordID));
    return buf;
  }

};

struct dhc_block {
  chordID id;
  ptr<keyhash_meta> meta;
  ptr<keyhash_data> data;
  uint size;
  u_char *buf;

  dhc_block (chordID ID) : id(ID), size (0), buf (NULL)
  {
    meta = New refcounted<keyhash_meta>;
    data = New refcounted<keyhash_data>;
  }

  dhc_block (char *bytes, int sz)
  {
    uint msize;
    size = sizeof (chordID);
    bcopy (bytes, &id, sizeof (chordID));
    bcopy (bytes + sizeof (chordID), &msize, sizeof (uint));
    size += msize;
    meta = New refcounted<keyhash_meta> (bytes + sizeof (chordID), msize);
    size += sizeof (u_int64_t) + sizeof (chordID);
    data = New refcounted<keyhash_data>;
    bcopy (bytes + sizeof (chordID) + msize, &data->tag.ver, sizeof (u_int64_t));
    bcopy (bytes + sizeof (chordID) + msize + sizeof (u_int64_t),
	   &data->tag.writer, sizeof (chordID));
    int data_size = sz - size;
    if (data_size >= 0)
      data->data.set (bytes + sizeof (chordID) + msize + sizeof (u_int64_t) + 
		      sizeof (chordID), data_size);
    else {
      warn << "dhc_block: Fatal exceeded end of pointer!!!\n";
      exit (-1);
    }
  }

  ~dhc_block () 
  {
    if (buf) free (buf);
    delete meta;
    delete data;
  }
  
  u_char *bytes ()
  {
    if (buf) free (buf);
    u_char *mbuf = meta->bytes ();
    size = sizeof (chordID) + meta->size + sizeof (u_int64_t) + 
      sizeof (chordID) + data->data.size ();
    buf = (u_char *) malloc (size);
    bcopy (&id, buf, sizeof (chordID));
    bcopy (mbuf, buf + sizeof (chordID), meta->size);
    bcopy (&data->tag.ver, buf + sizeof (chordID) + meta->size, 
	   sizeof (u_int64_t));
    bcopy (&data->tag.writer, buf + sizeof (chordID) + meta->size + 
	   sizeof (u_int64_t), sizeof (chordID));
    bcopy (data->data.base (), buf + sizeof (chordID) + meta->size + 
	   sizeof (u_int64_t) + sizeof (chordID), data->data.size ());
    return buf;
  }
  
};

struct paxos_state_t {
  bool recon_inprogress;
  uint promise_recvd;
  uint accept_recvd;
  vec<chordID> acc_conf;
  
  paxos_state_t () : recon_inprogress(false), promise_recvd(0), accept_recvd(0) {}
  
  ~paxos_state_t () { acc_conf.clear (); }
};

struct dhc_soft {
  chordID id;
  u_int64_t config_seqnum;
  vec<ptr<location> > config;
  vec<ptr<location> > new_config;   //next accepted config
  paxos_seqnum_t proposal;
  paxos_seqnum_t promised;

  ptr<paxos_state_t> pstat;
  
  ihash_entry <dhc_soft> link;

  dhc_soft (ptr<vnode> myNode, ptr<dhc_block> kb)
  {
    id = kb->id;
    config_seqnum = kb->meta->config.seqnum;
    set_locations (config, myNode, kb->meta->config.nodes);
    proposal.seqnum = 0;
    bzero (&proposal.proposer, sizeof (chordID));    
    promised.seqnum = kb->meta->accepted.seqnum;
    bcopy (&kb->meta->accepted.proposer, &promised.proposer, sizeof (chordID));
    
    pstat = New refcounted<paxos_state_t>;
  }
  
  ~dhc_soft () 
  {
    config.clear ();
    new_config.clear ();
  }
};

class dhc {
  
  ptr<vnode> myNode;
  ptr<dbfe> db;
  ihash<chordID, dhc_soft, &dhc_soft::id, &dhc_soft::link, hashID> dhcs;

  uint n_replica;

  void recv_prepare (user_args *);
  void recv_promise (chordID, ref<dhc_prepare_res>, clnt_stat);
  void recv_propose (user_args *);
  void recv_accept (chordID, ref<dhc_propose_res>, clnt_stat);
  void recv_newconfig (user_args *);
  void recv_newconfig_ack (chordID, ref<dhc_newconfig_res>, clnt_stat);
  
 public:

  dhc (ptr<vnode>, str, uint);
  ~dhc () {};
  
  void recon (chordID);
  void dispatch (user_args *);
  
  //just for testing...
  ref<dbfe> getdb () { return db; }
  
};

#endif /*_DHC_H_*/
