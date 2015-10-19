module OILR3.HostCompile (compileHostGraph) where

import OILR3.Instructions

import GPSyntax
import Graph
import Mapping

import Debug.Trace
import Data.List

data GraphElem = N NodeKey | E EdgeKey deriving (Eq, Ord, Show)
type GraphElemId = (RuleGraph, GraphElem)
type SemiOilrCode = [Instr GraphElemId GraphElemId]
type OilrCode = [Instr Int Int]

compileHostGraph :: HostGraph -> OilrCode
compileHostGraph g = nodes g ++ edges g

-- -------------------------------------------------------------------
-- host graph OILR instruction generation
-- -------------------------------------------------------------------

nodes :: HostGraph -> OilrCode
nodes g = concatMap node $ allNodes g
    where
        node (n, HostNode _ root (HostLabel [] c)) =
            ADN (nodeNumber n)
            : (if root then RTN (nodeNumber n) else NOP)
            : (if c == Uncoloured then NOP else CON (nodeNumber n) (definiteLookup c colourIds ) )
            : []


edges :: HostGraph -> OilrCode
edges g = map edge $ allEdges g
    where
        edge (e, _) = ADE (edgeNumber e) (nodeNumber $ source e) (nodeNumber $ target e)


