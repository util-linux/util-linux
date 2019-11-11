#irqtop
utility to display kernel interrupt information.

#example
```
irqtop - IRQ : 930, TOTAL : 27779096095, CPU : 96, ACTIVE CPU : 96
 IRQ        COUNT   DESC                                                        
 PIW       849463   Posted-interrupt wakeup event
 RES       243989   Rescheduling interrupts
 LOC       194051   Local timer interrupts
 PIN        76629   Posted-interrupt notification event
 280          902   IR-PCI-MSI 113246208-edge      mlx5_ctrl_eq@pci:0000:d8:00.0
 CAL          897   Function call interrupts
 154          143   IR-PCI-MSI 30932996-edge      eth0-3
 TLB          135   TLB shootdowns
 150          109   IR-PCI-MSI 30932992-edge      mlx5_ctrl_eq@pci:0000:3b:00.0
 215          108   IR-PCI-MSI 30935040-edge      mlx5_ctrl_eq@pci:0000:3b:00.1
 526          106   IR-PCI-MSI 113256448-edge      mlx5_ctrl_eq@pci:0000:d8:00.5
 846          106   IR-PCI-MSI 113276928-edge      mlx5_ctrl_eq@pci:0000:d8:01.7
 345          104   IR-PCI-MSI 113248256-edge      mlx5_ctrl_eq@pci:0000:d8:00.1
 430          102   IR-PCI-MSI 113250304-edge      mlx5_ctrl_eq@pci:0000:d8:00.2
 462          102   IR-PCI-MSI 113252352-edge      mlx5_ctrl_eq@pci:0000:d8:00.3
 494          102   IR-PCI-MSI 113254400-edge      mlx5_ctrl_eq@pci:0000:d8:00.4
 558          102   IR-PCI-MSI 113258496-edge      mlx5_ctrl_eq@pci:0000:d8:00.6
 590          102   IR-PCI-MSI 113260544-edge      mlx5_ctrl_eq@pci:0000:d8:00.7
 622          102   IR-PCI-MSI 113262592-edge      mlx5_ctrl_eq@pci:0000:d8:01.0
 654          102   IR-PCI-MSI 113264640-edge      mlx5_ctrl_eq@pci:0000:d8:01.1
 686          102   IR-PCI-MSI 113266688-edge      mlx5_ctrl_eq@pci:0000:d8:01.2
 718          102   IR-PCI-MSI 113268736-edge      mlx5_ctrl_eq@pci:0000:d8:01.3
 750          102   IR-PCI-MSI 113270784-edge      mlx5_ctrl_eq@pci:0000:d8:01.4
 782          102   IR-PCI-MSI 113272832-edge      mlx5_ctrl_eq@pci:0000:d8:01.5
 814          102   IR-PCI-MSI 113274880-edge      mlx5_ctrl_eq@pci:0000:d8:01.6
 878          102   IR-PCI-MSI 113278976-edge      mlx5_ctrl_eq@pci:0000:d8:02.0
 153           93   IR-PCI-MSI 30932995-edge      eth0-2
 155           84   IR-PCI-MSI 30932997-edge      eth0-4
 152           83   IR-PCI-MSI 30932994-edge      eth0-1
 157           75   IR-PCI-MSI 30932999-edge      eth0-6
 151           72   IR-PCI-MSI 30932993-edge      eth0-0
  94           54   IR-PCI-MSI 91750449-edge      nvme0q49
 156           48   IR-PCI-MSI 30932998-edge      eth0-5
 158           34   IR-PCI-MSI 30933000-edge      eth0-7
  99           15   IR-PCI-MSI 91750454-edge      nvme0q54
 NMI           15   Non-maskable interrupts
 PMI           15   Performance monitoring interrupts
  95           12   IR-PCI-MSI 91750450-edge      nvme0q50
 119           11   IR-PCI-MSI 91750466-edge      nvme0q66
   9           10   IR-IO-APIC    9-fasteoi   acpi
 128            3   IR-PCI-MSI 91750475-edge      nvme0q75
 123            2   IR-PCI-MSI 91750470-edge      nvme0q70
 141            2   IR-PCI-MSI 91750488-edge      nvme0q88
 281            2   IR-PCI-MSI 113246209-edge      eth2_15-0
  71            1   IR-PCI-MSI 91750426-edge      nvme0q26
 118            1   IR-PCI-MSI 91750465-edge      nvme0q65
   0            0   IR-IO-APIC    2-edge      timer
   8            0   IR-IO-APIC    8-edge      rtc0
  16            0   IR-IO-APIC   16-fasteoi   i801_smbus
  24            0   IR-PCI-MSI 458752-edge      PCIe PME, pciehp
  25            0   IR-PCI-MSI 468992-edge      PCIe PME
  27            0   IR-PCI-MSI 12058624-edge      PCIe PME, pciehp
  28            0   IR-PCI-MSI 12075008-edge      PCIe PME, pciehp
  30            0   IR-PCI-MSI 30408704-edge      PCIe PME
  31            0   IR-PCI-MSI 30441472-edge      PCIe PME
  34            0   IR-PCI-MSI 91226112-edge      PCIe PME, pciehp
  35            0   IR-PCI-MSI 91242496-edge      PCIe PME, pciehp
  36            0   IR-PCI-MSI 91258880-edge      PCIe PME, pciehp
  37            0   IR-PCI-MSI 91275264-edge      PCIe PME, pciehp
  39            0   IR-PCI-MSI 112721920-edge      PCIe PME
  40            0   IR-PCI-MSI 288768-edge      ahci[0000:00:11.5]
  41            0   IR-PCI-MSI 327680-edge      xhci_hcd
  43            0   IR-PCI-MSI 91750400-edge      nvme0q0
  44            0   IR-PCI-MSI 376832-edge      ahci[0000:00:17.0]
  46            0   IR-PCI-MSI 91750401-edge      nvme0q1
```