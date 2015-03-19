/*
 * Copyright (c) 2009-2010 Justin Holewinski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#if !defined(GRAPH_NODE_HXX_INC)
#define GRAPH_NODE_HXX_INC 1

namespace GraphAnalysis
{
	class GraphNode
	{

		public:

			/**
			 * Enumeration of graph node types.
			 */
			enum NodeType
			{
				NODE_TYPE_UNKNOWN = 0,  /**< Unknown node type. */

				NODE_TYPE_LOAD,         /**< Memory-read operation. */
				NODE_TYPE_STORE,        /**< Memory-write operation. */

				NODE_TYPE_FPADD,        /**< Floating-point add. */
				NODE_TYPE_FPSUB,        /**< Floating-point sub. */
				NODE_TYPE_FPMUL,        /**< Floating-point mul. */
				NODE_TYPE_FPDIV,        /**< Floating-point div. */

				NODE_TYPE_GENERIC,      /**< Generic type. */
				NODE_TYPE_PHI,           /**< Phi Nodes  */
				NODE_TYPE_GetElemPtr,
				NODE_TYPE_CAST,       
				NODE_TYPE_SELECT,       
				NODE_TYPE_CMP       
			};

			typedef std::vector<int32> PredecessorIDVector;
			//typedef std::vector<int64> NextDynNodeVector;  //vector of global ids of dynamic nodes in ddg, set during analysis
			typedef std::vector<GraphNode*> NextDynNodeVector;  //vector of global ids of dynamic nodes in ddg, set during analysis
			typedef std::vector<int64> ValueVector;


			/**
			 * Constructor.
			 */
			GraphNode();

			/**
			 * Destructor.
			 */
			~GraphNode();

			/**
			 * Gets the global ID.
			 *
			 * \return The global ID.
			 */
			inline int64 getGlobalID() const
			{
				return m_globalID;
			}

			/**
			 * Sets the global ID.
			 *
			 * \param[in] id  The global ID.
			 */
			inline void setGlobalID(int64 id)
			{
				m_globalID = id;
			}

			/**
			 * Gets the instruction ID.
			 *
			 * \return The instruction ID.
			 */
			inline int32 getInstructionID() const
			{
				return m_instructionID;
			}

			/**
			 * Sets the instruction ID.
			 *
			 * \param[in] id    The instruction ID.
			 */
			inline void setInstructionID(int32 id)
			{
				m_instructionID = id;
			}

			/**
			 * Gets the read/write address.
			 *
			 * \return The read/write address.
			 */
			inline byte* getAddress() const
			{
				return m_address;
			}

			/**
			 * Sets the read/write address.
			 *
			 * \param[in] addr    The read/write address.
			 */
			inline void setAddress(byte* addr)
			{
				m_address = addr;
			}

			/**
			 * Gets the type.
			 *
			 * \return The type.
			 */
			inline NodeType getType() const
			{
				return m_type;
			}

			/**
			 * Sets the type.
			 *
			 * \param[in] type  The type.
			 */
			inline void setType(NodeType type)
			{
				m_type = type;
			}

			/**
			 * Gets the number of sourrounding loops
			 *
			 * \return The number of sourrounding loops
			 */
			inline int32 getNumOfLoops() const
			{
				return m_numOfLoops;
			}


			/**
			 * Sets the number of surrounding loops
			 *
			 * \param[in] the number of sourrounding loops
			 */
			inline void setNumOfLoops(int32 nl)
			{
				m_numOfLoops = nl;
			}


			/**
			 * Sets the loop ID
			 *
			 * \param[in] the loop ID
			 */
			inline void setLoopID(int32 lid)
			{
				m_loopID = lid;
			}

			/**
			 * Gets the induction variable value vector
			 *
			 * \return The induction variable value vector
			 */
			inline const ValueVector& getInductionVariables() 
			{
				return  m_inductionVars;
			}
			inline void setInductionVariables(ValueVector v)
			{

				unsigned int i=0;
				for(i=0;i<v.size();i++){

					m_inductionVars.push_back( v[i] );
				}

			}
			
			inline void setSimplifiedInductionVariables(ValueVector v)
			{

				int i;
				for(i = v.size() - 2; i > 0; i -= 2){
					m_inductionVars.push_back( v[i] );
				}

			}

			/**
			 * Sets the induction variable value vector
			 *
			 * \param[in] the induction variable value vector
			 */
			inline void addInductionVariables(int64 iv)
			{
				m_inductionVars.push_back(iv);
			}
			/**
			 * Gets the container of predecessor node IDs.
			 *
			 * \return The container of predecessor node IDs.
			 */
			inline const PredecessorIDVector& getPredecessorIDs() const
			{
				return m_predecessorIDs;
			}

			/**
			 * Adds a node ID to the container of predecessors.
			 *
			 * \param[in] id  The node ID.
			 */
			inline void addPredecessorID(int32 id)
			{
				m_predecessorIDs.push_back(id);
			}

			/**
			 * Gets the container of predecessor node IDs.
			 *
			 * \return The container of predecessor node IDs.
			 */
			inline NextDynNodeVector& getEdges() 
			{
				return m_nextNodes;
			}


			/**
			 * Adds a node's global dynamic ID to the container of next nodes.
			 *
			 * \param[in] id  The node ID.
			 */
			inline bool addEdge(GraphNode* child, int type)
			{
				int i;
				bool alreadyIn=false;
				for(i=0; i < m_nextNodes.size();i++){
					int nid = (m_nextNodes[i])->getGlobalID();
					if(nid == (child->getGlobalID())){
						alreadyIn = true;
						break;
					}
				}		

				if(!alreadyIn)
					m_nextNodes.push_back(child);

				return alreadyIn;
			}

			inline NextDynNodeVector& getParents() 
			{
				return m_parentNodes;
			}

			inline void addParent(GraphNode* parent)
			{
				m_parentNodes.push_back(parent);
#if 0
				int i;
				bool alreadyIn=false;
				for(i=0; i < m_parentNodes.size();i++){
					int nid = (m_parentNodes[i]->getGlobalID());
					if(nid == (parent->getGlobalID())){
						alreadyIn = true;

						//std::cout<<"new parent already in "<< parent->getGlobalID() <<" & "<< ((m_parentNodes[i])->getGlobalID())<<std::endl;
						break;
					}
				}		

				if(!alreadyIn){
					m_parentNodes.push_back(parent);
					//std::cout<<"adding parent" <<parent->getGlobalID() <<"for " <<m_globalID<<std::endl;
				}
#endif
			}

			inline void delParent(int32 id)
			{
				int i;
				for(i=0; i < m_parentNodes.size();i++){
					if(((m_parentNodes[i])->getGlobalID()) == id){		
						if(i == (m_parentNodes.size()-1))
							m_parentNodes.pop_back();
						else
							m_parentNodes.erase(m_parentNodes.begin()+i);
						break;
					}

				}
			}

			inline std::vector<int>& getPreviousNodes() 
			{
				return m_prevNodes;
			}

			//add predecessor node	
			inline void addPrevious(int32 prev)
			{
				int i;
				bool alreadyIn=false;
				for(i=0; i < m_prevNodes.size();i++){
					if(m_prevNodes[i] == prev){
						alreadyIn = true;
						break;
					}
				}		
				if(!alreadyIn)
					m_prevNodes.push_back(prev);
			}


			inline NextDynNodeVector& getSiblings() 
			{
				return m_siblings;
			}

			//add sibling 	
			//inline void addSibling(int32 sib)
			inline void addSibling(GraphNode* sib)
			{
				int i;
				bool alreadyIn=false;
				for(i=0; i < m_siblings.size();i++){
					if(m_siblings[i]->getGlobalID() == sib->getGlobalID()){
						alreadyIn = true;
						break;
					}
				}		
				for(i=0; i < m_nextNodes.size();i++){		// check if a child
					if(m_nextNodes[i]->getGlobalID() == sib->getGlobalID()){
						alreadyIn = true;
						break;
					}
				}		
				if(!alreadyIn)
					m_siblings.push_back(sib);
			}


			//delete next node	
			inline void delEdge(int32 id)
			{
				int i,gid;
				std::cout.flush();
				for(i=0; i < m_nextNodes.size();i++){
					gid = (m_nextNodes[i])->getGlobalID();
					if(gid == id){		
						std::cout.flush();
						if(i == (m_nextNodes.size()-1))
							m_nextNodes.pop_back();
						else
							m_nextNodes.erase(m_nextNodes.begin()+i);
						break;
					}

				}
				std::cout.flush();
			}

			//delete predecessor node	
			inline void delPreviousNode(int32 id)
			{
				int i;
				for(i=0; i < m_prevNodes.size();i++){
					//std::cout<<" prev node "<<m_prevNodes[i]<<std::endl;
					if(m_prevNodes[i] == id){		
						//		    std::cout<<"in del pred id found id "<<std::endl;
						if(i == (m_prevNodes.size()-1))
							m_prevNodes.pop_back();
						else
							m_prevNodes.erase(m_prevNodes.begin()+i);
						break;
					}

				}
			}

			/**
			 * Increments the internal reference count.
			 */     
			inline void addRef()
			{
				m_refCount++;
			}

			/**
			 * Decrements the internal reference count and deletes the node if the count
			 * has reached zero.
			 */
			inline void release()
			{
				m_refCount--;

				if(m_refCount == 0)
				{
					delete this;
				}
			}

			inline void releaseAnyway(){

				delete this;
			}


			bool isEqualIndVars(GraphNode& node_b)
			{
				GraphNode::ValueVector::const_iterator  iter1,iter2;


				if(m_numOfLoops != node_b.getNumOfLoops())  // not at same loop depth
					return false;

				for(iter1 = m_inductionVars.begin(), iter2 = node_b.getInductionVariables().begin(); iter1 != m_inductionVars.end(); ++iter1, ++iter2)
				{
					if((*iter1) != (*iter2))
						return false;
				}

				return true;
			}

			inline void merge(GraphNode* b){

				
			}

			inline void addMemRead(){
				//std::cout<<"in increse count before "<<m_memRdCount<<std::endl;
				m_memRdCount++;
				//std::cout<<"in increse count after "<<m_memRdCount<<std::endl;
			}

			inline void delMemRead(){
				//std::cout<<"in increse count before "<<m_memRdCount<<std::endl;
				m_memRdCount--;
				//std::cout<<"in increse count after "<<m_memRdCount<<std::endl;
			}

			inline int getMemRdCount(){
				return m_memRdCount;
			}

			inline void setMemRdCount(int memcnt){
				m_memRdCount = memcnt;
			}

			inline void addFriend(GraphNode* sp){
				m_friend.insert(sp);
				m_mr = true;
			}
			inline std::set<GraphNode*>& getFriends(){ return m_friend;}
			inline void addMacro(GraphNode* sp){
				m_macro.insert(sp);
			}
			inline std::set<GraphNode*>& getMacro(){ return m_macro;}

			inline void setReady(bool rnr){ m_ready = rnr;}
			inline void setProcessed(bool unp){ m_processed = unp;}
			inline bool isReady(){ return m_ready;}	
			inline bool isProcessed(){ return m_processed;}	

			inline void setTileID(int tid){ m_tileID = tid;}
			inline int64 getTileID(){ return m_tileID;}
			inline void setIdx(int tid){ m_Idx = tid;}
			inline int64 getIdx(){ return m_Idx;}
			inline void setOpIdx(int tid){ m_opIdx = tid;}
			inline int64 getOpIdx(){ return m_opIdx;}

			inline void setLiveChildren(){ m_liveChildren = m_nextNodes.size();}
			inline int getLiveChildren(){ return m_liveChildren;}
			inline int reduceLiveChild(){ m_liveChildren--; return m_liveChildren;}

			inline void setUnprocParents(){
				m_unprocParents = 0;
				for(int i=0; i< m_parentNodes.size(); i++)
					//if((m_parentNodes[i]->getMemRdCount()) <= 0) // parent is not a load/store
						m_unprocParents++;
			}
			inline int32 getUnprocParents(){	return m_unprocParents;}
			inline void parentProcessed(){ m_unprocParents--;}
			inline void parentUnProcessed(){ m_unprocParents++;}
			inline int32 getVisitCount(){	return m_visitCount;}
			inline void visited(char* msg){ 
				m_visitCount++; 
				//std::cout<< m_globalID <<" visited "<<m_visitCount<< msg<<std::endl;
			}

			std::vector<byte*>& getLoads(){ return l_loads;}
			void insertLoads(byte* nla){ l_loads.push_back(nla); }

			std::vector<byte*>& getStores(){return s_stores;}
			void insertStores(byte* st){s_stores.push_back(st);}

			void setAccessType(int typ){ memAccessType = typ;}
			int getAccessType(){ return memAccessType;}

			void setMetisID(std::vector<char>& mid)
			{ m_metisID = mid;}
			std::vector<char>& getMetisID()
			{ return m_metisID;}

			public:
			bool m_visited ;                                    //TODO, remove this
			bool m_mr ;               
			private:

			int64                 m_globalID;
			int32                 m_instructionID;
			int32                 m_loopID;
			byte*                 m_address;			//if load/store, also used if next node is store
			std::vector<byte*>	  l_loads;			// if prev 2 loads
			std::vector<byte*>	  s_stores;			// all succesive stores
			int			  memAccessType;		// prev load/next store? type 1/2
			int			  store_id;		// prev load/next store? type 1/2
			NodeType              m_type;		
			PredecessorIDVector   m_predecessorIDs;
			int32                 m_refCount;
			int32                 m_numOfLoops;
			ValueVector           m_inductionVars; 
			NextDynNodeVector     m_nextNodes;
			NextDynNodeVector     m_parentNodes;
			NextDynNodeVector     m_siblings;
			//std::vector<int>    m_nextNodeType;
			std::vector<int32>    m_prevNodes;
			//GraphNode*          m_grp_leader_ID;
			std::set<GraphNode*>  m_friend;
			std::set<GraphNode*>  m_macro;
			std::map<int64,unsigned int> racount;
			int                   m_memRdCount;
			bool		  m_processed;
			bool                  m_ready;
			int			  m_liveChildren;
			int64		  m_tileID;
			int64		  m_Idx;
			int64		  m_opIdx;
			int32		  m_unprocParents;
			int32		  m_visitCount;;
			std::vector<char>	  m_metisID; 
			};
}

#endif

