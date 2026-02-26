ROIFT_GUI
- Implementar uma funcionalidade em que agrupe todas as máscaras e faça um mapa de calor, e que eu possa arrastar o mouse e clicar e ver quais imagens possue mascara no ponto. Mesmo que em um ponto tenha so uma imagem, torne-o visivel, para identificar outliers. 
    - Clicar botão direito no pixel  aparece uma opção "show mask lists" para ver qual das mascaras ocupa o pixel
    - Já existe um import csv que abre imagens, mas quero que faça o mesmo para mascaras, com os mesmos simbolos que tem das imagens.
    - Botao no menu principal chamado mask heatmap, e ele pega todas as máscaras na lista de máscaras e cria um viewer do heatmap. As dimensoes sempre serão 512x512, mas a altura precisa ser a maior altura das iamgens para não corta-los
