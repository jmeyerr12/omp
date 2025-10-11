# 🧠 Restaurar configurações de desempenho e Turbo Boost

# 🔹 Reativar Turbo Boost (Intel)
echo 0 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo

# 🔹 (Opcional) Reiniciar para garantir tudo restaurado
sudo reboot
