define i32 @test_licm_completo(i32 %n, i32 %a) {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %next_i, %body ]

  %invariante = add i32 %a, 10
  %costante = mul i32 3, 4
  %derivata = mul i32 %invariante, 2

  %confronto = icmp slt i32 %i, %n
  br i1 %confronto, label %body, label %exit

body:
  %solo_body = add i32 %a, 99
  %dipendente = add i32 %derivata, %i
  %risultato = add i32 %dipendente, %solo_body
  %next_i = add i32 %i, 1
  br label %header

exit:
  ret i32 %i
}