; Input minimale per provare Loop-Invariant Code Motion.
; %invariant dipende solo da %a e %b, che sono definiti fuori dal loop.
; Dopo l'implementazione dovrebbe poter essere spostata nel preheader.

define i32 @foo(i32 %a, i32 %b, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %updated, %loop ]
  %invariant = mul i32 %a, %b
  %updated = add i32 %sum, %invariant
  %next = add i32 %i, 1
  %condition = icmp slt i32 %next, %n
  br i1 %condition, label %loop, label %exit

exit:
  ret i32 %updated
}
