# Semi-automated script (see instructions/comments) for setting up kernel for editing syscalls

wget https://www.kernel.org/pub/linux/kernel/v6.x/linux-6.1.106.tar.xz
tar -xJf linux-6.1.106.tar.xz
cd linux-6.1.106
make clean && make mrproper

make localmodconfig
# ^^^ Accept the defaults (repeatedly press Enter) during this step

make menuconfig
# ^^^ ----- Follow these steps -----
# a.  Hit  ↵ Enter  on the selected “General setup ----->” 
# b.  Use the down arrow key to highlight “Local version – append to kernel release” 
# c.  Hit   ↵ Enter  
# d.  Type -csc452 in the box (note the leading dash). 
# e.  Hit   ↵ Enter  
# f.  Use the down arrow key to highlight “Automatically append version information to the version string” 
# g.  Hit   Spacebar  so that the item is now selected, and shows [*] next to it 
# h.  Use the down arrow key to highlight “Kernel compression mode (XZ) ----->” 
# i.  Hit   ↵ Enter  
# j.  Use arrow keys to select “Bzip2”. Hit   ↵ Enter . 
# k.  Use right arrow key to select “Save” (hit right twice) 
# l.  ↵ Enter  to select “< Ok >” 
# m.  Right arrow to select “Exit”. ↵ Enter . 
# n.  Right arrow to select “Exit”. ↵ Enter . 
# o.  You should now be back at the normal command line

make -j 3
# (^^^ This may take time, maybe hours)

make modules_install

cd ..
rm linux-6.1.106.tar.xz