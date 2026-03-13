-- Example from the help message.

local r = execute {
  data={ [42] = 0x100 },
  src=[[
    @mov     r1      42
    @read    r2      r1

    @id      r3

    add (8) r4 r2<8;8,1> r3<8;8,1> {A@1}

    @write   r3       r4
    @eot
  ]]
}

dump(r, 4)
