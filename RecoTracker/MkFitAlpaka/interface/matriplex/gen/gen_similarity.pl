#!/usr/bin/env perl
# mkfitdev (MkFitAlpaka): generic dense 6x6 similarity  out = A * B * A^T  (A full 6x6, B and out symmetric),
# generated with stock GenMul.pm exactly as GenMPlexOps.pl generates MultHelixProp / MultHelixPropTransp,
# but with no sparsity pattern. Run from this directory:  perl -I. gen_similarity.pl ; mv *.ah ..
use lib ".";
use GenMul;
use warnings;

$A = new GenMul::Matrix('name'=>'a', 'M'=>6, 'N'=>6);
$B = new GenMul::MatrixSym('name'=>'b', 'M'=>6, 'N'=>6);
$C = new GenMul::Matrix('name'=>'c', 'M'=>6, 'N'=>6);

$m = new GenMul::Multiply;
$m->dump_multiply_portable("SimilarityLL.ah", $A, $B, $C);

$AT = new GenMul::MatrixTranspose($A);
$Out = new GenMul::MatrixSym('name'=>'c', 'M'=>6, 'N'=>6);
$C->{name} = 'b';
$m->dump_multiply_portable("SimilarityLLTransp.ah", $C, $AT, $Out);
